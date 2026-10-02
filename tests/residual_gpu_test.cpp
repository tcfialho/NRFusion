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

#include "residual_gpu_test_dispatch.inc"
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
