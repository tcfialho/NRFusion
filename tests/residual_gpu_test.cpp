#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include "nrfusion/ResidualEngine.hpp"
#include "nrfusion/ResidualReprojection.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct ReprojectCB {
    std::uint32_t width;
    std::uint32_t height;
    float currentBlend;
    float minConfidence;
    float depthRelativeThreshold;
    float maxMotionPixels;
    float currentPreExposure;
    float historyPreExposure;
    std::uint32_t cameraCut;
    std::uint32_t historyValid;
    float padding[2];
};
static_assert(sizeof(ReprojectCB) == 48);

std::string ReadShaderSource(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

int main() {
    std::cout << "[Residual GPU Test] Initializing numerical parity validation..." << std::endl;

    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        std::cerr << "Failed to create DXGI factory" << std::endl;
        return 1;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            std::wcout << L"[Residual GPU Test] Hardware GPU: " << desc.Description << std::endl;
            break;
        }
    }

    if (!adapter) {
        std::cerr << "No hardware DX12 adapter found" << std::endl;
        return 1;
    }

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::cerr << "Failed to create D3D12 device" << std::endl;
        return 1;
    }

    // Create Compute Queue & Allocator & Command List
    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue)))) return 1;

    ComPtr<ID3D12CommandAllocator> alloc;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&alloc)))) return 1;

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, alloc.Get(), nullptr, IID_PPV_ARGS(&cmdList)))) return 1;

    // Load and compile shader
    std::string hlsl = ReadShaderSource("shaders/ResidualReproject_CS.hlsl");
    if (hlsl.empty()) {
        hlsl = ReadShaderSource("../shaders/ResidualReproject_CS.hlsl");
    }
    if (hlsl.empty()) {
        hlsl = ReadShaderSource("../../shaders/ResidualReproject_CS.hlsl");
    }
    if (hlsl.empty()) {
        hlsl = ReadShaderSource("../../../shaders/ResidualReproject_CS.hlsl");
    }
    if (hlsl.empty()) {
        std::cerr << "Could not open shaders/ResidualReproject_CS.hlsl" << std::endl;
        return 1;
    }

    ComPtr<ID3DBlob> csBlob, errorBlob;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(hlsl.c_str(), hlsl.length(), "ResidualReproject_CS.hlsl", nullptr, nullptr,
                          "CSMain", "cs_5_0", compileFlags, 0, &csBlob, &errorBlob))) {
        if (errorBlob) std::cerr << "Shader compile error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        return 1;
    }

    // Root Signature:
    // Slot 0: CBV (b0)
    // Slot 1: Descriptor Table (6 SRVs: t0..t5)
    // Slot 2: Descriptor Table (2 UAVs: u0..u1)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 6;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 2;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc{};
    rsDesc.NumParameters = 3;
    rsDesc.pParameters = params;

    ComPtr<ID3DBlob> sigBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob))) return 1;

    ComPtr<ID3D12RootSignature> rootSig;
    if (FAILED(device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSig)))) return 1;

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSig.Get();
    psoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso)))) return 1;

    // Test Resolution
    constexpr std::uint32_t W = 64;
    constexpr std::uint32_t H = 64;
    constexpr std::size_t N = W * H;

    // Setup CPU Reference Input
    nrfusion::ResidualReprojectionConfig reprojConfig;
    reprojConfig.currentBlend = 0.08f;
    reprojConfig.minConfidence = 0.25f;
    reprojConfig.depthRelativeThreshold = 0.04f;
    reprojConfig.maxMotionPixels = 512.0f;
    nrfusion::ResidualReprojection cpuReproject(reprojConfig);

    nrfusion::ResidualReprojectionInput input;
    input.current.width = W;
    input.current.height = H;
    input.current.sourceFrame = 2;
    input.current.preExposure = 1.0f;
    input.current.residual.resize(N);
    input.current.depth.resize(N);

    input.history.width = W;
    input.history.height = H;
    input.history.sourceFrame = 1;
    input.history.preExposure = 1.0f;
    input.history.residual.resize(N);
    input.history.depth.resize(N);

    input.motionX.resize(N);
    input.motionY.resize(N);
    input.confidence.resize(N);
    input.cameraCut = false;

    // Populate with diverse test data
    for (std::uint32_t y = 0; y < H; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            input.current.residual[i] = std::sin(float(x) * 0.2f) * std::cos(float(y) * 0.2f);
            input.current.depth[i] = 1.0f + float(x + y) * 0.05f;

            input.history.residual[i] = std::cos(float(x) * 0.15f) * std::sin(float(y) * 0.15f);
            input.history.depth[i] = 1.0f + float(x + y) * 0.05f;

            // Motion of ~1.5 pixels
            input.motionX[i] = 1.5f;
            input.motionY[i] = 0.8f;
            // Half pixels high confidence, half low confidence
            input.confidence[i] = (x % 2 == 0) ? 0.9f : 0.1f;
        }
    }

    // Run CPU Reference
    const auto cpuOutput = cpuReproject.Run(input);

    // Create GPU Textures (W=64 * sizeof(float) = 256 bytes per row, exactly pitch aligned)
    D3D12_HEAP_PROPERTIES defaultHeap{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_HEAP_PROPERTIES uploadHeap{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_HEAP_PROPERTIES readbackHeap{D3D12_HEAP_TYPE_READBACK};

    auto CreateTex2D = [&](DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) -> ComPtr<ID3D12Resource> {
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = W;
        td.Height = H;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = format;
        td.SampleDesc.Count = 1;
        td.Flags = flags;
        ComPtr<ID3D12Resource> res;
        device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &td,
                                       D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&res));
        return res;
    };

    auto CreateBuffer = [&](UINT64 size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state) -> ComPtr<ID3D12Resource> {
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = size;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES hp{heapType};
        ComPtr<ID3D12Resource> res;
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, state, nullptr, IID_PPV_ARGS(&res));
        return res;
    };

    auto tCurrRes = CreateTex2D(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE);
    auto tCurrDepth = CreateTex2D(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE);
    auto tHistRes = CreateTex2D(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE);
    auto tHistDepth = CreateTex2D(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE);
    auto tMotion = CreateTex2D(DXGI_FORMAT_R32G32_FLOAT, D3D12_RESOURCE_FLAG_NONE);
    auto tConf = CreateTex2D(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE);

    auto tOutRes = CreateTex2D(DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto tOutAcc = CreateTex2D(DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Upload buffers
    const UINT64 f32Size = W * H * sizeof(float);
    const UINT64 f32x2Size = W * H * sizeof(float) * 2;
    auto uCurrRes = CreateBuffer(f32Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto uCurrDepth = CreateBuffer(f32Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto uHistRes = CreateBuffer(f32Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto uHistDepth = CreateBuffer(f32Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto uMotion = CreateBuffer(f32x2Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto uConf = CreateBuffer(f32Size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

    // Copy CPU data to upload buffers
    void* ptr = nullptr;
    uCurrRes->Map(0, nullptr, &ptr); memcpy(ptr, input.current.residual.data(), f32Size); uCurrRes->Unmap(0, nullptr);
    uCurrDepth->Map(0, nullptr, &ptr); memcpy(ptr, input.current.depth.data(), f32Size); uCurrDepth->Unmap(0, nullptr);
    uHistRes->Map(0, nullptr, &ptr); memcpy(ptr, input.history.residual.data(), f32Size); uHistRes->Unmap(0, nullptr);
    uHistDepth->Map(0, nullptr, &ptr); memcpy(ptr, input.history.depth.data(), f32Size); uHistDepth->Unmap(0, nullptr);

    std::vector<float> motionInterleaved(N * 2);
    for (size_t i = 0; i < N; ++i) {
        motionInterleaved[i * 2 + 0] = input.motionX[i];
        motionInterleaved[i * 2 + 1] = input.motionY[i];
    }
    uMotion->Map(0, nullptr, &ptr); memcpy(ptr, motionInterleaved.data(), f32x2Size); uMotion->Unmap(0, nullptr);
    uConf->Map(0, nullptr, &ptr); memcpy(ptr, input.confidence.data(), f32Size); uConf->Unmap(0, nullptr);

    // Constant Buffer (256-byte aligned)
    auto cbRes = CreateBuffer(256, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ReprojectCB cbData{};
    cbData.width = W;
    cbData.height = H;
    cbData.currentBlend = reprojConfig.currentBlend;
    cbData.minConfidence = reprojConfig.minConfidence;
    cbData.depthRelativeThreshold = reprojConfig.depthRelativeThreshold;
    cbData.maxMotionPixels = reprojConfig.maxMotionPixels;
    cbData.currentPreExposure = input.current.preExposure;
    cbData.historyPreExposure = input.history.preExposure;
    cbData.cameraCut = input.cameraCut ? 1 : 0;
    cbData.historyValid = 1;
    cbRes->Map(0, nullptr, &ptr); memcpy(ptr, &cbData, sizeof(cbData)); cbRes->Unmap(0, nullptr);

    // Descriptor Heaps
    D3D12_DESCRIPTOR_HEAP_DESC srvUavHeapDesc{};
    srvUavHeapDesc.NumDescriptors = 8;
    srvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    device->CreateDescriptorHeap(&srvUavHeapDesc, IID_PPV_ARGS(&heap));
    const UINT dSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    auto cpuHandle = heap->GetCPUDescriptorHandleForHeapStart();
    auto CreateSRV = [&](ID3D12Resource* res, DXGI_FORMAT format) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = format;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(res, &srv, cpuHandle);
        cpuHandle.ptr += dSize;
    };
    CreateSRV(tCurrRes.Get(), DXGI_FORMAT_R32_FLOAT);
    CreateSRV(tCurrDepth.Get(), DXGI_FORMAT_R32_FLOAT);
    CreateSRV(tHistRes.Get(), DXGI_FORMAT_R32_FLOAT);
    CreateSRV(tHistDepth.Get(), DXGI_FORMAT_R32_FLOAT);
    CreateSRV(tMotion.Get(), DXGI_FORMAT_R32G32_FLOAT);
    CreateSRV(tConf.Get(), DXGI_FORMAT_R32_FLOAT);

    auto CreateUAV = [&](ID3D12Resource* res, DXGI_FORMAT format) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = format;
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(res, nullptr, &uav, cpuHandle);
        cpuHandle.ptr += dSize;
    };
    CreateUAV(tOutRes.Get(), DXGI_FORMAT_R32_FLOAT);
    CreateUAV(tOutAcc.Get(), DXGI_FORMAT_R32_UINT);

    // Record upload & dispatch
    auto CopyToTexture = [&](ID3D12Resource* dst, ID3D12Resource* src, DXGI_FORMAT fmt, UINT rowPitch) {
        D3D12_TEXTURE_COPY_LOCATION dstLoc{}, srcLoc{};
        dstLoc.pResource = dst;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        srcLoc.pResource = src;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Format = fmt;
        srcLoc.PlacedFootprint.Footprint.Width = W;
        srcLoc.PlacedFootprint.Footprint.Height = H;
        srcLoc.PlacedFootprint.Footprint.Depth = 1;
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = dst;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmdList->ResourceBarrier(1, &b);

        cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, &b);
    };

    CopyToTexture(tCurrRes.Get(), uCurrRes.Get(), DXGI_FORMAT_R32_FLOAT, W * 4);
    CopyToTexture(tCurrDepth.Get(), uCurrDepth.Get(), DXGI_FORMAT_R32_FLOAT, W * 4);
    CopyToTexture(tHistRes.Get(), uHistRes.Get(), DXGI_FORMAT_R32_FLOAT, W * 4);
    CopyToTexture(tHistDepth.Get(), uHistDepth.Get(), DXGI_FORMAT_R32_FLOAT, W * 4);
    CopyToTexture(tMotion.Get(), uMotion.Get(), DXGI_FORMAT_R32G32_FLOAT, W * 8);
    CopyToTexture(tConf.Get(), uConf.Get(), DXGI_FORMAT_R32_FLOAT, W * 4);

    // Transition UAV textures to UNORDERED_ACCESS
    D3D12_RESOURCE_BARRIER uavBarriers[2]{};
    uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    uavBarriers[0].Transition.pResource = tOutRes.Get();
    uavBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    uavBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    uavBarriers[1].Transition.pResource = tOutAcc.Get();
    uavBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    uavBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cmdList->ResourceBarrier(2, uavBarriers);

    // Dispatch
    ID3D12DescriptorHeap* heaps[] = {heap.Get()};
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(rootSig.Get());
    cmdList->SetPipelineState(pso.Get());

    cmdList->SetComputeRootConstantBufferView(0, cbRes->GetGPUVirtualAddress());
    cmdList->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
    auto uavGpu = heap->GetGPUDescriptorHandleForHeapStart();
    uavGpu.ptr += 6 * dSize;
    cmdList->SetComputeRootDescriptorTable(2, uavGpu);

    cmdList->Dispatch((W + 15) / 16, (H + 15) / 16, 1);

    // Readback
    auto rOutRes = CreateBuffer(f32Size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    auto rOutAcc = CreateBuffer(f32Size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);

    auto CopyFromTexture = [&](ID3D12Resource* dstBuf, ID3D12Resource* srcTex, DXGI_FORMAT fmt, UINT rowPitch) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = srcTex;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmdList->ResourceBarrier(1, &b);

        D3D12_TEXTURE_COPY_LOCATION dstLoc{}, srcLoc{};
        dstLoc.pResource = dstBuf;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstLoc.PlacedFootprint.Offset = 0;
        dstLoc.PlacedFootprint.Footprint.Format = fmt;
        dstLoc.PlacedFootprint.Footprint.Width = W;
        dstLoc.PlacedFootprint.Footprint.Height = H;
        dstLoc.PlacedFootprint.Footprint.Depth = 1;
        dstLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

        srcLoc.pResource = srcTex;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;

        cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    };

    CopyFromTexture(rOutRes.Get(), tOutRes.Get(), DXGI_FORMAT_R32_FLOAT, W * 4);
    CopyFromTexture(rOutAcc.Get(), tOutAcc.Get(), DXGI_FORMAT_R32_UINT, W * 4);

    cmdList->Close();
    ID3D12CommandList* lists[] = {cmdList.Get()};
    queue->ExecuteCommandLists(1, lists);

    // Wait on fence
    ComPtr<ID3D12Fence> fence;
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    queue->Signal(fence.Get(), 1);
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, evt);
    WaitForSingleObject(evt, 2000);
    CloseHandle(evt);

    // Read back results
    std::vector<float> gpuResidual(N);
    std::vector<std::uint32_t> gpuAccepted(N);

    rOutRes->Map(0, nullptr, &ptr);
    memcpy(gpuResidual.data(), ptr, f32Size);
    rOutRes->Unmap(0, nullptr);

    rOutAcc->Map(0, nullptr, &ptr);
    memcpy(gpuAccepted.data(), ptr, f32Size);
    rOutAcc->Unmap(0, nullptr);

    // NUMERICAL VERIFICATION TARGET: CPU vs GPU PARITY
    double maxDiff = 0.0;
    std::uint32_t acceptedMatches = 0;
    std::uint32_t acceptedTotal = 0;

    for (std::size_t i = 0; i < N; ++i) {
        const float cpuVal = cpuOutput.image.residual[i];
        const float gpuVal = gpuResidual[i];
        const double diff = std::fabs(static_cast<double>(cpuVal) - static_cast<double>(gpuVal));
        if (diff > maxDiff) {
            maxDiff = diff;
        }

        const std::uint8_t cpuAcc = cpuOutput.historyAccepted[i];
        const std::uint32_t gpuAcc = gpuAccepted[i];
        assert((cpuAcc != 0) == (gpuAcc != 0));
        if (cpuAcc != 0) ++acceptedTotal;
        if ((cpuAcc != 0) == (gpuAcc != 0)) ++acceptedMatches;
    }

    std::cout << "[Residual GPU Test] Verified " << N << " pixels (" << W << "x" << H << ")." << std::endl;
    std::cout << "[Residual GPU Test] Acceptance flag match: " << acceptedMatches << "/" << N
              << " (" << acceptedTotal << " accepted)" << std::endl;
    std::cout << "[Residual GPU Test] Maximum numerical diff: " << maxDiff << std::endl;

    assert(acceptedMatches == N);
    assert(maxDiff < 1.0e-5);

    std::cout << "SUCCESS: D3D12 GPU Residual Compute Shader passed exact numerical parity test." << std::endl;
    return 0;
}
