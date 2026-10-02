#include "nrfusion/D3D12NrCodec.hpp"

#include "DlssNr_Shader.h"
#include "DlssNr_NoKeep.h"
#include "dlssnr_residual_Shader.h"

namespace nrfusion {

D3D12NrCodec::~D3D12NrCodec() noexcept {
    Shutdown();
}

bool D3D12NrCodec::Init(ID3D12Device* device) noexcept {
    if (Ready()) return device == device_;
    if (device == nullptr) return false;

    device_ = device;
    device_->AddRef();
    slotIndex_ = 0;
    ResetSlotCache();
    descriptorSize_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    if (!CreateRootSignature() || !CreatePipelines() || !CreateSlots()) {
        Shutdown();
        return false;
    }
    return true;
}

bool D3D12NrCodec::CreateRootSignature() noexcept {
    D3D12_DESCRIPTOR_RANGE1 ranges[3]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = kSrvCount;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = kUavCount;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    ranges[2].NumDescriptors = kCbvCount;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 3;
    parameter.DescriptorTable.pDescriptorRanges = ranges;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = 1;
    desc.Desc_1_1.pParameters = &parameter;
    desc.Desc_1_1.NumStaticSamplers = 1;
    desc.Desc_1_1.pStaticSamplers = &sampler;

    ID3DBlob* signature = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT serialized =
        D3D12SerializeVersionedRootSignature(&desc, &signature, &errors);
    if (errors != nullptr) errors->Release();
    if (FAILED(serialized) || signature == nullptr) {
        if (signature != nullptr) signature->Release();
        return false;
    }

    const HRESULT created = device_->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSignature_));
    signature->Release();
    return SUCCEEDED(created) && rootSignature_ != nullptr;
}

bool D3D12NrCodec::CreatePipelines() noexcept {
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_;
    desc.CS.pShaderBytecode = DlssNr_cso;
    desc.CS.BytecodeLength = sizeof(DlssNr_cso);
    if (FAILED(device_->CreateComputePipelineState(
            &desc, IID_PPV_ARGS(&pipelineState_))) || pipelineState_ == nullptr)
        return false;

    desc.CS.pShaderBytecode = dlssnr_residual_cso;
    desc.CS.BytecodeLength = sizeof(dlssnr_residual_cso);
    if (FAILED(device_->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(&residualPipelineState_))) || !residualPipelineState_) return false;
    desc.CS.pShaderBytecode = DlssNr_no_keep_cso;
    desc.CS.BytecodeLength = sizeof(DlssNr_no_keep_cso);
    const HRESULT optionalCreated = device_->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(&encodeNoKeepPipelineState_));
    if (FAILED(optionalCreated) && encodeNoKeepPipelineState_) {
        encodeNoKeepPipelineState_->Release();
        encodeNoKeepPipelineState_ = nullptr;
    }
    return true;
}

bool D3D12NrCodec::CreateSlots() noexcept {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = kDescriptorCount * kSlotCount;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(
            &heapDesc, IID_PPV_ARGS(&descriptorHeap_))))
        return false;

    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = sizeof(D3D12NrCodecConstants);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (std::uint32_t slotNumber = 0; slotNumber < kSlotCount; ++slotNumber) {
        Slot& slot = slots_[slotNumber];
        if (FAILED(device_->CreateCommittedResource(
                &upload, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&slot.constants))))
            return false;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(slot.constants->Map(0, &noRead, &slot.mappedConstants)) ||
            slot.mappedConstants == nullptr)
            return false;
        D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
        desc.BufferLocation = slot.constants->GetGPUVirtualAddress();
        desc.SizeInBytes = static_cast<UINT>(sizeof(D3D12NrCodecConstants));
        device_->CreateConstantBufferView(
            &desc, Handle(slotNumber, kSrvCount + kUavCount));
    }
    return true;
}

D3D12NrCodecAccounting D3D12NrCodec::Accounting() const noexcept {
    D3D12NrCodecAccounting result{};
    result.descriptorHeapCount = descriptorHeap_ != nullptr ? 1u : 0u;
    for (const Slot& slot : slots_) {
        if (slot.constants == nullptr) continue;
        ++result.resourceCount;
        if (slot.mappedConstants != nullptr) ++result.mappedConstantBufferCount;
        result.logicalBytes += sizeof(D3D12NrCodecConstants);
    }
    return result;
}

void D3D12NrCodec::ResetSlotCache() noexcept {
    for (Slot& slot : slots_) {
        for (auto& s : slot.srvs) s = nullptr;
        for (auto& u : slot.uavs) u = nullptr;
    }
    for (auto& entry : descCache_) entry = {};
}

void D3D12NrCodec::Shutdown() noexcept {
    ResetSlotCache();
    for (Slot& slot : slots_) {
        if (slot.mappedConstants != nullptr && slot.constants != nullptr)
            slot.constants->Unmap(0, nullptr);
        if (slot.constants != nullptr) slot.constants->Release();
        slot = {};
    }
    if (descriptorHeap_ != nullptr) descriptorHeap_->Release();
    if (residualPipelineState_ != nullptr) residualPipelineState_->Release();
    if (encodeNoKeepPipelineState_ != nullptr) encodeNoKeepPipelineState_->Release();
    if (pipelineState_ != nullptr) pipelineState_->Release();
    if (rootSignature_ != nullptr) rootSignature_->Release();
    if (device_ != nullptr) device_->Release();
    descriptorHeap_ = nullptr;
    residualPipelineState_ = nullptr;
    encodeNoKeepPipelineState_ = nullptr;
    pipelineState_ = nullptr;
    rootSignature_ = nullptr;
    device_ = nullptr;
    descriptorSize_ = 0;
    slotIndex_ = 0;
}

} // namespace nrfusion
