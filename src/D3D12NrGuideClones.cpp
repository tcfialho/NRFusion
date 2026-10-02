#include "nrfusion/D3D12NrGuideClones.hpp"
#include "nrfusion/D3D12NrAllocationTracker.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace nrfusion {
namespace {

const char* KindToString(D3D12NrGuideKind kind) noexcept {
    switch (kind) {
    case D3D12NrGuideKind::Depth: return "Depth";
    case D3D12NrGuideKind::Motion: return "Motion";
    }
    return "Unknown";
}

std::uint32_t BytesPerPixel(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R16_UNORM:
        return 2;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return 4;
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 0;
    }
}

} // namespace

bool D3D12NrGuideClones::SameDesc(
    const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) noexcept {
    return a.Dimension == b.Dimension &&
           a.Alignment == b.Alignment &&
           a.Width == b.Width &&
           a.Height == b.Height &&
           a.DepthOrArraySize == b.DepthOrArraySize &&
           a.MipLevels == b.MipLevels &&
           a.Format == b.Format &&
           a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality &&
           a.Layout == b.Layout &&
           a.Flags == b.Flags;
}

ID3D12Resource* D3D12NrGuideClones::Create(
    ID3D12Device* device, const D3D12_RESOURCE_DESC& desc,
    std::uint64_t* outPhysicalBytes) noexcept {
    if (device == nullptr || desc.Format == DXGI_FORMAT_UNKNOWN ||
        desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        desc.Width == 0 || desc.Height == 0)
        return nullptr;

    if (outPhysicalBytes != nullptr) {
        const auto allocInfo = device->GetResourceAllocationInfo(0, 1, &desc);
        *outPhysicalBytes = (allocInfo.SizeInBytes != (std::numeric_limits<std::uint64_t>::max)())
                                ? allocInfo.SizeInBytes
                                : 0;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* resource = nullptr;
    const HRESULT result = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&resource));
    return SUCCEEDED(result) ? resource : nullptr;
}

std::uint64_t D3D12NrGuideClones::LogicalBytes(const Clone& clone) noexcept {
    if (clone.resource == nullptr || clone.desc.DepthOrArraySize != 1 ||
        clone.desc.MipLevels != 1 || clone.desc.SampleDesc.Count != 1)
        return 0;
    const std::uint32_t bytesPerPixel = BytesPerPixel(clone.desc.Format);
    if (bytesPerPixel == 0) return 0;
    const std::uint64_t pixels =
        clone.desc.Width * static_cast<std::uint64_t>(clone.desc.Height);
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    return pixels > max / bytesPerPixel ? 0 : pixels * bytesPerPixel;
}

bool D3D12NrGuideClones::Park(
    Clone& clone, NrDeferredRetirementQueue& retirement,
    D3D12NrGuideKind kind,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    if (clone.resource == nullptr) return true;
    D3D12NrAllocationTracker::Instance().RecordRetirement("GuideClone", KindToString(kind));
    void* object = clone.resource;
    if (!retirement.Park(
            object, NrRetiredObjectKind::Resource,
            NrDeferredRetirementQueue::kDefaultDelay, LogicalBytes(clone),
            clone.physicalBytes, fence, fenceValue))
        return false;
    clone = {};
    return true;
}

void D3D12NrGuideClones::Release(Clone& clone) noexcept {
    if (clone.resource != nullptr) clone.resource->Release();
    clone = {};
}

D3D12NrGuideClones::Clone* D3D12NrGuideClones::Slot(D3D12NrGuideKind kind) noexcept {
    switch (kind) {
    case D3D12NrGuideKind::Depth: return &depth_;
    case D3D12NrGuideKind::Motion: return &motion_;
    }
    return nullptr;
}

const D3D12NrGuideClones::Clone* D3D12NrGuideClones::Slot(
    D3D12NrGuideKind kind) const noexcept {
    switch (kind) {
    case D3D12NrGuideKind::Depth: return &depth_;
    case D3D12NrGuideKind::Motion: return &motion_;
    }
    return nullptr;
}

bool D3D12NrGuideClones::Ensure(
    ID3D12Device* device, D3D12NrGuideKind kind,
    ID3D12Resource* source, DXGI_FORMAT typedFormat,
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    Clone* clone = Slot(kind);
    if (device == nullptr || clone == nullptr || source == nullptr ||
        typedFormat == DXGI_FORMAT_UNKNOWN)
        return false;

    D3D12_RESOURCE_DESC wanted = source->GetDesc();
    wanted.Format = typedFormat;
    wanted.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (clone->resource != nullptr && SameDesc(clone->desc, wanted)) return true;
    if (clone->resource != nullptr &&
        retirement.Size() == NrDeferredRetirementQueue::kCapacity)
        return false;

    std::uint64_t physicalBytes = 0;
    ID3D12Resource* resource = Create(device, wanted, &physicalBytes);
    if (resource == nullptr) return false;
    if (!Park(*clone, retirement, kind, fence, fenceValue)) {
        resource->Release();
        return false;
    }

    clone->resource = resource;
    clone->desc = wanted;
    clone->state = D3D12_RESOURCE_STATE_COPY_DEST;
    clone->physicalBytes = physicalBytes;
    D3D12NrAllocationTracker::Instance().RecordAllocation(
        "GuideClone", KindToString(kind), wanted.Format,
        static_cast<std::uint32_t>(wanted.Width), wanted.Height,
        LogicalBytes(*clone), physicalBytes, true, true);
    return true;
}

bool D3D12NrGuideClones::Retire(
    D3D12NrGuideKind kind, NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    Clone* clone = Slot(kind);
    if (clone == nullptr) return false;
    if (clone->resource != nullptr &&
        retirement.Size() == NrDeferredRetirementQueue::kCapacity)
        return false;
    return Park(*clone, retirement, kind, fence, fenceValue);
}

bool D3D12NrGuideClones::Retire(
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    const std::size_t active =
        static_cast<std::size_t>(depth_.resource != nullptr) +
        static_cast<std::size_t>(motion_.resource != nullptr);
    if (active > NrDeferredRetirementQueue::kCapacity - retirement.Size()) return false;
    return Park(depth_, retirement, D3D12NrGuideKind::Depth, fence, fenceValue) &&
           Park(motion_, retirement, D3D12NrGuideKind::Motion, fence, fenceValue);
}

bool D3D12NrGuideClones::Transition(
    ID3D12GraphicsCommandList* cmdList, D3D12NrGuideKind kind,
    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept {
    Clone* clone = Slot(kind);
    if (cmdList == nullptr || clone == nullptr ||
        clone->resource == nullptr || clone->state != expected)
        return false;
    if (expected == next) return true;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = clone->resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = expected;
    barrier.Transition.StateAfter = next;
    cmdList->ResourceBarrier(1, &barrier);
    clone->state = next;
    return true;
}

void D3D12NrGuideClones::ReleaseAfterIdle() noexcept {
    Release(depth_);
    Release(motion_);
}

ID3D12Resource* D3D12NrGuideClones::Get(D3D12NrGuideKind kind) const noexcept {
    const Clone* clone = Slot(kind);
    return clone == nullptr ? nullptr : clone->resource;
}

D3D12_RESOURCE_STATES D3D12NrGuideClones::State(D3D12NrGuideKind kind) const noexcept {
    const Clone* clone = Slot(kind);
    return clone == nullptr ? D3D12_RESOURCE_STATE_COMMON : clone->state;
}

D3D12NrGuideCloneAccounting D3D12NrGuideClones::Accounting() const noexcept {
    D3D12NrGuideCloneAccounting result{};
    const Clone* clones[] = {&depth_, &motion_};
    const auto max = (std::numeric_limits<std::uint64_t>::max)();

    for (const Clone* clone : clones) {
        if (clone->resource == nullptr) continue;
        ++result.resourceCount;

        const std::uint64_t bytes = LogicalBytes(*clone);
        if (bytes == 0) {
            result.logicalBytesExact = false;
        } else if (result.logicalBytes > max - bytes) {
            result.logicalBytes = max;
            result.logicalBytesExact = false;
        } else {
            result.logicalBytes += bytes;
        }

        const std::uint64_t phys = clone->physicalBytes;
        if (phys == 0) {
            result.physicalBytesExact = false;
        } else if (result.physicalBytes > max - phys) {
            result.physicalBytes = max;
            result.physicalBytesExact = false;
        } else {
            result.physicalBytes += phys;
        }
    }

    peakLogicalBytes_ = (std::max)(peakLogicalBytes_, result.logicalBytes);
    peakPhysicalBytes_ = (std::max)(peakPhysicalBytes_, result.physicalBytes);
    result.peakLogicalBytes = peakLogicalBytes_;
    result.peakPhysicalBytes = peakPhysicalBytes_;

    return result;
}

std::uint64_t D3D12NrGuideClones::PhysicalBytes(D3D12NrGuideKind kind) const noexcept {
    const Clone* clone = Slot(kind);
    return clone ? clone->physicalBytes : 0;
}

} // namespace nrfusion
