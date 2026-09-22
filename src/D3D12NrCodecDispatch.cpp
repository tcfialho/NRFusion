#include "nrfusion/D3D12NrCodec.hpp"

#include <cstring>

namespace nrfusion {

D3D12_CPU_DESCRIPTOR_HANDLE D3D12NrCodec::Handle(
    const Slot& slot, std::uint32_t index) const noexcept {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = slot.heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
    return handle;
}

DXGI_FORMAT D3D12NrCodec::TypedFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R32G32B32_TYPELESS: return DXGI_FORMAT_R32G32B32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UINT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    default: return format;
    }
}

bool D3D12NrCodec::WriteSrv(
    ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept {
    if (resource == nullptr) return false;
    const D3D12_RESOURCE_DESC source = resource->GetDesc();
    if (source.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        source.SampleDesc.Count != 1 ||
        (source.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0)
        return false;

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = TypedFormat(source.Format);
    if (desc.Format == DXGI_FORMAT_UNKNOWN) return false;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = source.MipLevels == 0 ? UINT(-1) : source.MipLevels;
    device_->CreateShaderResourceView(resource, &desc, handle);
    return true;
}

bool D3D12NrCodec::WriteUav(
    ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept {
    if (resource == nullptr) return false;
    const D3D12_RESOURCE_DESC source = resource->GetDesc();
    if (source.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        source.SampleDesc.Count != 1 ||
        (source.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
        return false;

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = TypedFormat(source.Format);
    if (desc.Format == DXGI_FORMAT_UNKNOWN) return false;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_->CreateUnorderedAccessView(resource, nullptr, &desc, handle);
    return true;
}

bool D3D12NrCodec::WriteConstants(
    Slot& slot, const D3D12NrCodecConstants& constants,
    D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept {
    void* mapped = nullptr;
    const D3D12_RANGE noRead{0, 0};
    if (FAILED(slot.constants->Map(0, &noRead, &mapped)) || mapped == nullptr) return false;
    std::memcpy(mapped, &constants, sizeof(constants));
    slot.constants->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC desc{};
    desc.BufferLocation = slot.constants->GetGPUVirtualAddress();
    desc.SizeInBytes = static_cast<UINT>(sizeof(constants));
    device_->CreateConstantBufferView(&desc, handle);
    return true;
}

bool D3D12NrCodec::Dispatch(
    ID3D12GraphicsCommandList* commandList,
    const D3D12NrCodecConstants& constants,
    const D3D12NrCodecResources& resources) noexcept {
    if (!Ready() || commandList == nullptr || resources.source == nullptr ||
        resources.target == nullptr || constants.width == 0 || constants.height == 0 ||
        constants.mode > static_cast<std::uint32_t>(D3D12NrCodecMode::ZeroMotion))
        return false;

    Slot& slot = slots_[slotIndex_];
    slotIndex_ = (slotIndex_ + 1) % kSlotCount;

    ID3D12Resource* srvs[kSrvCount] = {
        resources.source,
        resources.model != nullptr ? resources.model : resources.source,
        resources.original != nullptr ? resources.original : resources.source,
        resources.motion != nullptr ? resources.motion : resources.source,
        resources.previousEdit != nullptr ? resources.previousEdit : resources.source,
    };
    ID3D12Resource* uavs[kUavCount] = {
        resources.target,
        resources.keep != nullptr ? resources.keep : resources.target,
    };

    for (std::uint32_t i = 0; i < kSrvCount; ++i) {
        if (!WriteSrv(srvs[i], Handle(slot, i))) return false;
    }
    for (std::uint32_t i = 0; i < kUavCount; ++i) {
        if (!WriteUav(uavs[i], Handle(slot, kSrvCount + i))) return false;
    }
    if (!WriteConstants(slot, constants, Handle(slot, kSrvCount + kUavCount)))
        return false;

    ID3D12DescriptorHeap* heaps[] = {slot.heap};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(rootSignature_);
    commandList->SetPipelineState(pipelineState_);
    commandList->SetComputeRootDescriptorTable(
        0, slot.heap->GetGPUDescriptorHandleForHeapStart());
    const UINT groupsX = constants.width / 8u + static_cast<UINT>(constants.width % 8u != 0);
    const UINT groupsY = constants.height / 8u + static_cast<UINT>(constants.height % 8u != 0);
    commandList->Dispatch(groupsX, groupsY, 1);
    return true;
}

} // namespace nrfusion
