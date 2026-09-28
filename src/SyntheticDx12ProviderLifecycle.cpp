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
    auto failInitialization = [this]() {
        for (auto& slot : ringSlots_) {
            slot.lowColor.Reset();
            slot.lowDepth.Reset();
            slot.lowMotion.Reset();
            slot.lowNeuralOut.Reset();
            slot.lowResidual.Reset();
            slot.inUse = false;
            slot.activeWorkId = 0;
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
        nextFenceValue_ = 1;
        ready_ = false;
        return false;
    };
    device_ = static_cast<ID3D12Device*>(context.device);
    if (context.commandQueue) {
        queue_ = static_cast<ID3D12CommandQueue*>(context.commandQueue);
    }

    if (FAILED(device_->CreateFence(
            0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        return failInitialization();
    }

    if (!EnsureShaders()) return failInitialization();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = 64;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(
            &heapDesc, IID_PPV_ARGS(&srvUavHeap_)))) {
        return failInitialization();
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


} // namespace nrfusion
