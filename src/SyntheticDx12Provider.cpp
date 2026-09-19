#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/MatchedResidualShader.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

// Only one compiler understands a library request written in the source. Elsewhere it is an
// unknown pragma, which a build with warnings as errors refuses outright.
#if defined(_MSC_VER)
#pragma comment(lib, "d3dcompiler.lib")
#endif

namespace nrfusion {

namespace {

const char* g_matchedResidualShader = MatchedResidualShaderSource();

} // namespace

SyntheticDx12Provider::SyntheticDx12Provider() = default;

SyntheticDx12Provider::~SyntheticDx12Provider() {
    Shutdown();
}

bool SyntheticDx12Provider::Initialize(const ProviderContext& context) {
    std::scoped_lock lock(mutex_);
    if (ready_) return true;

    if (!context.device) return false;
    device_ = static_cast<ID3D12Device*>(context.device);
    if (context.commandQueue) {
        queue_ = static_cast<ID3D12CommandQueue*>(context.commandQueue);
    }

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        return false;
    }

    if (!EnsureShaders()) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 64;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&srvUavHeap_)))) {
        return false;
    }
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ready_ = true;
    return true;
}

void SyntheticDx12Provider::Shutdown() {
    std::scoped_lock lock(mutex_);
    if (!ready_) return;

    if (queue_ && fence_) {
        uint64_t waitVal = nextFenceValue_++;
        queue_->Signal(fence_.Get(), waitVal);
        if (fence_->GetCompletedValue() < waitVal) {
            HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (evt) {
                fence_->SetEventOnCompletion(waitVal, evt);
                WaitForSingleObject(evt, 2000);
                CloseHandle(evt);
            }
        }
    }

    for (auto& s : ringSlots_) {
        s.lowColor.Reset();
        s.lowDepth.Reset();
        s.lowMotion.Reset();
        s.lowNeuralOut.Reset();
        s.lowResidual.Reset();
        s.inUse = false;
        s.activeWorkId = 0;
    }

    srvUavHeap_.Reset();
    descriptorSize_ = 0;
    downsamplePso_.Reset();
    extractResidualPso_.Reset();
    composeResidualPso_.Reset();
    rootSignature_.Reset();
    fence_.Reset();
    queue_.Reset();
    device_.Reset();
    ready_ = false;
}

bool SyntheticDx12Provider::EnsureShaders() {
    if (rootSignature_ && downsamplePso_ && extractResidualPso_ && composeResidualPso_) {
        return true;
    }

    // Root parameters:
    // Param 0: 32-bit constants (b0) - 8 DWORDs
    // Param 1: Descriptor table (2 SRVs: t0, t1)
    // Param 2: Descriptor table (1 UAV: u0)
    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;
    srvRange.BaseShaderRegister = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 8;
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

    ComPtr<ID3DBlob> sigBlob, errBlob;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errBlob))) {
        return false;
    }
    if (FAILED(device_->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)))) {
        return false;
    }

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

    // Compile CSDownsample
    ComPtr<ID3DBlob> downBlob;
    if (FAILED(D3DCompile(g_matchedResidualShader, strlen(g_matchedResidualShader), "MatchedResidual.hlsl",
                          nullptr, nullptr, "CSDownsample", "cs_5_0", compileFlags, 0, &downBlob, &errBlob))) {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC downDesc{};
    downDesc.pRootSignature = rootSignature_.Get();
    downDesc.CS = { downBlob->GetBufferPointer(), downBlob->GetBufferSize() };
    if (FAILED(device_->CreateComputePipelineState(&downDesc, IID_PPV_ARGS(&downsamplePso_)))) {
        return false;
    }

    // Compile CSExtractResidual
    ComPtr<ID3DBlob> extBlob;
    if (FAILED(D3DCompile(g_matchedResidualShader, strlen(g_matchedResidualShader), "MatchedResidual.hlsl",
                          nullptr, nullptr, "CSExtractResidual", "cs_5_0", compileFlags, 0, &extBlob, &errBlob))) {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC extDesc{};
    extDesc.pRootSignature = rootSignature_.Get();
    extDesc.CS = { extBlob->GetBufferPointer(), extBlob->GetBufferSize() };
    if (FAILED(device_->CreateComputePipelineState(&extDesc, IID_PPV_ARGS(&extractResidualPso_)))) {
        return false;
    }

    // Compile CSComposeResidual
    ComPtr<ID3DBlob> compBlob;
    if (FAILED(D3DCompile(g_matchedResidualShader, strlen(g_matchedResidualShader), "MatchedResidual.hlsl",
                          nullptr, nullptr, "CSComposeResidual", "cs_5_0", compileFlags, 0, &compBlob, &errBlob))) {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC compDesc{};
    compDesc.pRootSignature = rootSignature_.Get();
    compDesc.CS = { compBlob->GetBufferPointer(), compBlob->GetBufferSize() };
    if (FAILED(device_->CreateComputePipelineState(&compDesc, IID_PPV_ARGS(&composeResidualPso_)))) {
        return false;
    }

    return true;
}

bool SyntheticDx12Provider::EnsureSlotResources(uint32_t slot, Resolution workRes) {
    if (slot >= kRingSlots || !workRes.Valid()) return false;
    Slot& s = ringSlots_[slot];
    if (s.resolution == workRes && s.lowColor && s.lowResidual && s.lowNeuralOut) {
        return true;
    }

    s.lowColor.Reset();
    s.lowDepth.Reset();
    s.lowMotion.Reset();
    s.lowNeuralOut.Reset();
    s.lowResidual.Reset();

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = workRes.width;
    resDesc.Height = workRes.height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.SampleDesc.Count = 1;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    // 1. lowColor
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&s.lowColor)))) return false;

    // 2. lowNeuralOut
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&s.lowNeuralOut)))) return false;

    // 3. lowResidual
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               IID_PPV_ARGS(&s.lowResidual)))) return false;

    s.resolution = workRes;
    return true;
}

void SyntheticDx12Provider::Transition(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* res,
                                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    if (!cmdList || !res || before == after) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &b);
}

SyntheticWorkHandle SyntheticDx12Provider::Submit(const SyntheticFrameInputs& inputs, void* cmdListPtr) {
    std::scoped_lock lock(mutex_);
    SyntheticWorkHandle handle{};
    if (!ready_ || !inputs.Valid()) return handle;

    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);
    if (!cmdList) return handle;

    Resolution workRes = SyntheticDlaaContract::CalculateWorkingResolution(inputs.renderResolution, inputs.workingScale);
    if (!workRes.Valid()) return handle;

    uint32_t slotIdx = currentSlot_;
    currentSlot_ = (currentSlot_ + 1) % kRingSlots;

    if (!EnsureSlotResources(slotIdx, workRes)) {
        return handle;
    }

    Slot& slot = ringSlots_[slotIdx];
    slot.activeWorkId = inputs.ticket.id;
    slot.inUse = true;

    // If workingScale < 1.0, run GPU pre-downsample. If workingScale == 1.0,
    // initialize lowColor explicitly as well: the neural executor consumes lowColor
    // in both modes, so "native resolution" is not permission to leave it unwritten.
    if (inputs.workingScale < 0.999f) {
        auto* origColorRes = reinterpret_cast<ID3D12Resource*>(inputs.color.opaqueId);
        if (!origColorRes || !srvUavHeap_) {
            slot.activeWorkId = 0;
            slot.inUse = false;
            return handle;
        }
        if (origColorRes && srvUavHeap_) {
            Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            // 16 descriptors reserved per ring slot: Submit uses +0..2, ComposeNative +4..6,
            // ExtractResidual +8..10 -- kept apart so no two of a frame's own dispatches ever
            // overwrite a descriptor another one of that same frame's dispatches still needs.
            uint32_t baseDesc = slotIdx * 16;
            auto cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
            auto gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();

            D3D12_CPU_DESCRIPTOR_HANDLE downSrvCpu = cpuHandle;
            downSrvCpu.ptr += (baseDesc + 0) * descriptorSize_;
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            device_->CreateShaderResourceView(origColorRes, &srvDesc, downSrvCpu);

            D3D12_CPU_DESCRIPTOR_HANDLE downSrv1Cpu = cpuHandle;
            downSrv1Cpu.ptr += (baseDesc + 1) * descriptorSize_;
            device_->CreateShaderResourceView(origColorRes, &srvDesc, downSrv1Cpu);

            D3D12_CPU_DESCRIPTOR_HANDLE downUavCpu = cpuHandle;
            downUavCpu.ptr += (baseDesc + 2) * descriptorSize_;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            device_->CreateUnorderedAccessView(slot.lowColor.Get(), nullptr, &uavDesc, downUavCpu);

            ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
            cmdList->SetDescriptorHeaps(1, heaps);
            cmdList->SetComputeRootSignature(rootSignature_.Get());
            cmdList->SetPipelineState(downsamplePso_.Get());

            struct {
                uint32_t srcW, srcH, dstW, dstH;
                float pad[4];
            } downCb{ inputs.renderResolution.width, inputs.renderResolution.height, workRes.width, workRes.height, {0} };
            cmdList->SetComputeRoot32BitConstants(0, 8, &downCb, 0);

            D3D12_GPU_DESCRIPTOR_HANDLE downSrvGpu = gpuHandle;
            downSrvGpu.ptr += (baseDesc + 0) * descriptorSize_;
            cmdList->SetComputeRootDescriptorTable(1, downSrvGpu);

            D3D12_GPU_DESCRIPTOR_HANDLE downUavGpu = gpuHandle;
            downUavGpu.ptr += (baseDesc + 2) * descriptorSize_;
            cmdList->SetComputeRootDescriptorTable(2, downUavGpu);

            cmdList->Dispatch((workRes.width + 15) / 16, (workRes.height + 15) / 16, 1);

            Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        }
    } else {
        auto* origColorRes = reinterpret_cast<ID3D12Resource*>(inputs.color.opaqueId);
        if (!origColorRes) {
            slot.activeWorkId = 0;
            slot.inUse = false;
            return handle;
        }

        const D3D12_RESOURCE_DESC sourceDesc = origColorRes->GetDesc();
        const D3D12_RESOURCE_DESC targetDesc = slot.lowColor->GetDesc();
        if (sourceDesc.Width != targetDesc.Width || sourceDesc.Height != targetDesc.Height ||
            sourceDesc.Format != targetDesc.Format || sourceDesc.SampleDesc.Count != targetDesc.SampleDesc.Count) {
            slot.activeWorkId = 0;
            slot.inUse = false;
            return handle;
        }

        Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyResource(slot.lowColor.Get(), origColorRes);
        Transition(cmdList, origColorRes, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }

    handle.workId = inputs.ticket.id;
    handle.valid = true;
    handle.workingScale = inputs.workingScale;
    handle.workResolution = workRes;
    handle.nativeResolution = inputs.renderResolution;

    uint64_t fVal = nextFenceValue_++;
    slot.fenceValue = fVal;
    handle.fenceValue = fVal;

    return handle;
}

bool SyntheticDx12Provider::Poll(const SyntheticWorkHandle& handle) {
    if (!ready_ || !handle.valid || !fence_) return false;
    return fence_->GetCompletedValue() >= handle.fenceValue;
}

ResourceRef SyntheticDx12Provider::GetResidual(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;

    for (const auto& s : ringSlots_) {
        if (s.activeWorkId == handle.workId && s.lowResidual) {
            ref.opaqueId = reinterpret_cast<uint64_t>(s.lowResidual.Get());
            ref.resolution = s.resolution;
            ref.format = ResourceFormat::Rgba16Float;
            return ref;
        }
    }
    return ref;
}

uint32_t SyntheticDx12Provider::SlotForWork(uint64_t workId) const {
    for (uint32_t i = 0; i < ringSlots_.size(); ++i) {
        if (ringSlots_[i].activeWorkId == workId) return i;
    }
    return kRingSlots;
}

bool SyntheticDx12Provider::ExtractResidual(const SyntheticWorkHandle& handle, void* cmdListPtr) {
    std::scoped_lock lock(mutex_);
    if (!ready_ || !handle.valid || !cmdListPtr || !srvUavHeap_) return false;
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    const uint32_t slotIdx = SlotForWork(handle.workId);
    if (slotIdx >= kRingSlots) return false;
    Slot& slot = ringSlots_[slotIdx];
    if (!slot.lowColor || !slot.lowNeuralOut || !slot.lowResidual) return false;

    Transition(cmdList, slot.lowNeuralOut.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList, slot.lowResidual.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Same 16-descriptor-per-slot layout Submit/ComposeNative use; +8..10 is this dispatch's own
    // sub-range so it never overwrites a descriptor either of those still needs this frame.
    uint32_t baseDesc = slotIdx * 16 + 8;
    auto cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
    auto gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srv0Cpu = cpuHandle;
    srv0Cpu.ptr += (baseDesc + 0) * descriptorSize_;
    device_->CreateShaderResourceView(slot.lowNeuralOut.Get(), &srvDesc, srv0Cpu);

    D3D12_CPU_DESCRIPTOR_HANDLE srv1Cpu = cpuHandle;
    srv1Cpu.ptr += (baseDesc + 1) * descriptorSize_;
    device_->CreateShaderResourceView(slot.lowColor.Get(), &srvDesc, srv1Cpu);

    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuHandle;
    uavCpu.ptr += (baseDesc + 2) * descriptorSize_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(slot.lowResidual.Get(), nullptr, &uavDesc, uavCpu);

    ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetComputeRootSignature(rootSignature_.Get());
    cmdList->SetPipelineState(extractResidualPso_.Get());

    struct {
        uint32_t workWidth, workHeight;
        float preExposure, padding;
        float pad2[4];
    } cb{ slot.resolution.width, slot.resolution.height, 1.0f, 0.0f, {0} };
    cmdList->SetComputeRoot32BitConstants(0, 8, &cb, 0);

    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = gpuHandle;
    srvGpu.ptr += (baseDesc + 0) * descriptorSize_;
    cmdList->SetComputeRootDescriptorTable(1, srvGpu);

    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = gpuHandle;
    uavGpu.ptr += (baseDesc + 2) * descriptorSize_;
    cmdList->SetComputeRootDescriptorTable(2, uavGpu);

    cmdList->Dispatch((slot.resolution.width + 15) / 16, (slot.resolution.height + 15) / 16, 1);

    Transition(cmdList, slot.lowNeuralOut.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, slot.lowColor.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, slot.lowResidual.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    return true;
}

bool SyntheticDx12Provider::ComposeNative(const SyntheticWorkHandle& handle,
                                          const ResourceRef& originalNative,
                                          const ResourceRef& destinationNative,
                                          void* cmdListPtr,
                                          float residualWeight) {
    std::scoped_lock lock(mutex_);
    if (!ready_ || !handle.valid || !cmdListPtr) return false;
    auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(cmdListPtr);

    auto* origRes = reinterpret_cast<ID3D12Resource*>(originalNative.opaqueId);
    auto* dstRes = reinterpret_cast<ID3D12Resource*>(destinationNative.opaqueId);
    if (!origRes || !dstRes) return false;

    ID3D12Resource* residualRes = nullptr;
    uint32_t slotIdx = 0;
    for (size_t i = 0; i < ringSlots_.size(); ++i) {
        if (ringSlots_[i].activeWorkId == handle.workId) {
            residualRes = ringSlots_[i].lowResidual.Get();
            slotIdx = static_cast<uint32_t>(i);
            break;
        }
    }
    if (!residualRes) return false;

    // Transition states
    Transition(cmdList, dstRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Transition(cmdList, origRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Transition(cmdList, residualRes, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    struct {
        uint32_t nativeW;
        uint32_t nativeH;
        uint32_t resW;
        uint32_t resH;
        float weight;
        float exposure;
        float pad0;
        float pad1;
    } cb{
        originalNative.resolution.width,
        originalNative.resolution.height,
        handle.workResolution.width,
        handle.workResolution.height,
        residualWeight,
        1.0f,
        0.0f,
        0.0f
    };

    if (srvUavHeap_) {
        uint32_t baseDesc = slotIdx * 16 + 4;
        auto cpuHandle = srvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        auto gpuHandle = srvUavHeap_->GetGPUDescriptorHandleForHeapStart();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_CPU_DESCRIPTOR_HANDLE srv0Cpu = cpuHandle;
        srv0Cpu.ptr += (baseDesc + 0) * descriptorSize_;
        device_->CreateShaderResourceView(origRes, &srvDesc, srv0Cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE srv1Cpu = cpuHandle;
        srv1Cpu.ptr += (baseDesc + 1) * descriptorSize_;
        device_->CreateShaderResourceView(residualRes, &srvDesc, srv1Cpu);

        D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuHandle;
        uavCpu.ptr += (baseDesc + 2) * descriptorSize_;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device_->CreateUnorderedAccessView(dstRes, nullptr, &uavDesc, uavCpu);

        ID3D12DescriptorHeap* heaps[] = { srvUavHeap_.Get() };
        cmdList->SetDescriptorHeaps(1, heaps);
        cmdList->SetComputeRootSignature(rootSignature_.Get());
        cmdList->SetPipelineState(composeResidualPso_.Get());
        cmdList->SetComputeRoot32BitConstants(0, 8, &cb, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = gpuHandle;
        srvGpu.ptr += (baseDesc + 0) * descriptorSize_;
        cmdList->SetComputeRootDescriptorTable(1, srvGpu);

        D3D12_GPU_DESCRIPTOR_HANDLE uavGpu = gpuHandle;
        uavGpu.ptr += (baseDesc + 2) * descriptorSize_;
        cmdList->SetComputeRootDescriptorTable(2, uavGpu);

        uint32_t dispatchX = (originalNative.resolution.width + 15) / 16;
        uint32_t dispatchY = (originalNative.resolution.height + 15) / 16;
        cmdList->Dispatch(dispatchX, dispatchY, 1);
    }

    Transition(cmdList, dstRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, origRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    Transition(cmdList, residualRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    return true;
}

ID3D12Resource* SyntheticDx12Provider::GetSlotLowColor(uint32_t slot) const {
    if (slot >= kRingSlots) return nullptr;
    return ringSlots_[slot].lowColor.Get();
}

ID3D12Resource* SyntheticDx12Provider::GetSlotLowResidual(uint32_t slot) const {
    if (slot >= kRingSlots) return nullptr;
    return ringSlots_[slot].lowResidual.Get();
}

ID3D12Resource* SyntheticDx12Provider::GetSlotLowNeuralOut(uint32_t slot) const {
    if (slot >= kRingSlots) return nullptr;
    return ringSlots_[slot].lowNeuralOut.Get();
}

uint64_t SyntheticDx12Provider::CompletedFenceValue() const {
    if (!fence_) return 0;
    return fence_->GetCompletedValue();
}

} // namespace nrfusion
