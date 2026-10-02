#include "nrfusion/D3D12NrScratchResources.hpp"
#include "nrfusion/D3D12NrAllocationTracker.hpp"

namespace nrfusion {
namespace {

bool Valid(DXGI_FORMAT format, std::uint32_t width, std::uint32_t height) noexcept {
    return format != DXGI_FORMAT_UNKNOWN && width != 0 && height != 0;
}

bool Valid(const D3D12NrScratchDesc& desc) noexcept {
    return Valid(desc.format, desc.frameWidth, desc.frameHeight) &&
           desc.workWidth != 0 && desc.workHeight != 0;
}

constexpr std::uint32_t BytesPerPixel(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R32G32_FLOAT:
        return 8;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
        return 4;
    default:
        return 0;
    }
}

} // namespace

D3D12NrScratchResources::Surface D3D12NrScratchResources::MakeSurface(
    ID3D12Resource* resource, DXGI_FORMAT format,
    std::uint32_t width, std::uint32_t height,
    std::uint64_t physicalBytes) noexcept {
    Surface surface{};
    surface.resource = resource;
    surface.format = format;
    surface.width = width;
    surface.height = height;
    surface.physicalBytes = physicalBytes;
    return surface;
}

ID3D12Resource* D3D12NrScratchResources::Create(
    ID3D12Device* device, DXGI_FORMAT format,
    std::uint32_t width, std::uint32_t height,
    std::uint64_t* outPhysicalBytes) noexcept {
    if (device == nullptr || !Valid(format, width, height)) return nullptr;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (outPhysicalBytes != nullptr) {
        const D3D12_RESOURCE_ALLOCATION_INFO allocInfo = device->GetResourceAllocationInfo(0, 1, &desc);
        *outPhysicalBytes = (allocInfo.SizeInBytes != 0 && allocInfo.SizeInBytes != static_cast<UINT64>(-1))
            ? allocInfo.SizeInBytes
            : (static_cast<std::uint64_t>(width) * height * BytesPerPixel(format));
    }

    ID3D12Resource* resource = nullptr;
    const HRESULT result = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&resource));
    return SUCCEEDED(result) ? resource : nullptr;
}

void D3D12NrScratchResources::Release(Surface& surface) noexcept {
    if (surface.resource != nullptr) surface.resource->Release();
    surface = {};
}

std::uint64_t D3D12NrScratchResources::PhysicalBytes(const Surface& surface) noexcept {
    if (surface.resource == nullptr) return 0;
    return surface.physicalBytes != 0 ? surface.physicalBytes : LogicalBytes(surface);
}

std::uint64_t D3D12NrScratchResources::PhysicalBytes(D3D12NrScratchKind kind) const noexcept {
    const Surface* surface = Slot(kind);
    return surface == nullptr ? 0 : PhysicalBytes(*surface);
}

bool D3D12NrScratchResources::Park(
    Surface& surface, NrDeferredRetirementQueue& retirement,
    const char* kindName,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    if (surface.resource == nullptr) return true;
    void* object = surface.resource;
    if (!retirement.Park(object, NrRetiredObjectKind::Resource,
                         NrDeferredRetirementQueue::kDefaultDelay,
                         LogicalBytes(surface),
                         surface.physicalBytes,
                         fence,
                         fenceValue)) return false;
    if (kindName != nullptr) {
        D3D12NrAllocationTracker::Instance().RecordRetirement("Scratch", kindName);
    }
    surface = {};
    return true;
}

bool D3D12NrScratchResources::IsCoreKind(D3D12NrScratchKind kind) noexcept {
    return kind == D3D12NrScratchKind::Output ||
           kind == D3D12NrScratchKind::ColorCopy ||
           kind == D3D12NrScratchKind::HdrCopy;
}

D3D12NrScratchResources::Surface* D3D12NrScratchResources::Slot(
    D3D12NrScratchKind kind) noexcept {
    switch (kind) {
    case D3D12NrScratchKind::Output: return &output_;
    case D3D12NrScratchKind::ColorCopy: return &colorCopy_;
    case D3D12NrScratchKind::HdrCopy: return &hdrCopy_;
    case D3D12NrScratchKind::PassScratch: return &passScratch_;
    case D3D12NrScratchKind::ColorSmall: return &colorSmall_;
    case D3D12NrScratchKind::OutputNative: return &outputNative_;
    case D3D12NrScratchKind::ActiveColor: return &activeColor_;
    case D3D12NrScratchKind::ResidualEdited: return &residualEdited_;
    case D3D12NrScratchKind::ResidualHistory0: return &residualHistory0_;
    case D3D12NrScratchKind::ResidualHistory1: return &residualHistory1_;
    case D3D12NrScratchKind::ResidualComposed: return &residualComposed_;
    }
    return nullptr;
}

const D3D12NrScratchResources::Surface* D3D12NrScratchResources::Slot(
    D3D12NrScratchKind kind) const noexcept {
    return const_cast<D3D12NrScratchResources*>(this)->Slot(kind);
}

std::size_t D3D12NrScratchResources::ActiveCount() const noexcept {
    return static_cast<std::size_t>(output_.resource != nullptr) +
           static_cast<std::size_t>(colorCopy_.resource != nullptr) +
           static_cast<std::size_t>(hdrCopy_.resource != nullptr) +
           static_cast<std::size_t>(passScratch_.resource != nullptr) +
           static_cast<std::size_t>(colorSmall_.resource != nullptr) +
           static_cast<std::size_t>(outputNative_.resource != nullptr) +
           static_cast<std::size_t>(activeColor_.resource != nullptr) +
           static_cast<std::size_t>(residualEdited_.resource != nullptr) +
           static_cast<std::size_t>(residualHistory0_.resource != nullptr) +
           static_cast<std::size_t>(residualHistory1_.resource != nullptr) +
           static_cast<std::size_t>(residualComposed_.resource != nullptr);
}

bool D3D12NrScratchResources::ParkAll(
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    if (ActiveCount() > NrDeferredRetirementQueue::kCapacity - retirement.Size()) return false;
    return Park(output_, retirement, KindToString(D3D12NrScratchKind::Output), fence, fenceValue) &&
           Park(colorCopy_, retirement, KindToString(D3D12NrScratchKind::ColorCopy), fence, fenceValue) &&
           Park(hdrCopy_, retirement, KindToString(D3D12NrScratchKind::HdrCopy), fence, fenceValue) &&
           Park(passScratch_, retirement, KindToString(D3D12NrScratchKind::PassScratch), fence, fenceValue) &&
           Park(colorSmall_, retirement, KindToString(D3D12NrScratchKind::ColorSmall), fence, fenceValue) &&
           Park(outputNative_, retirement, KindToString(D3D12NrScratchKind::OutputNative), fence, fenceValue) &&
           Park(activeColor_, retirement, KindToString(D3D12NrScratchKind::ActiveColor), fence, fenceValue) &&
           Park(residualEdited_, retirement, KindToString(D3D12NrScratchKind::ResidualEdited), fence, fenceValue) &&
           Park(residualHistory0_, retirement, KindToString(D3D12NrScratchKind::ResidualHistory0), fence, fenceValue) &&
           Park(residualHistory1_, retirement, KindToString(D3D12NrScratchKind::ResidualHistory1), fence, fenceValue) &&
           Park(residualComposed_, retirement, KindToString(D3D12NrScratchKind::ResidualComposed), fence, fenceValue);
}

bool D3D12NrScratchResources::Complete() const noexcept {
    return output_.resource != nullptr &&
           colorCopy_.resource != nullptr &&
           (!desc_.needsHdrCopy || hdrCopy_.resource != nullptr);
}

bool D3D12NrScratchResources::Matches(const D3D12NrScratchDesc& desc) const noexcept {
    return Complete() && desc_ == desc;
}

bool D3D12NrScratchResources::EnsureOptional(
    ID3D12Device* device, D3D12NrScratchKind kind,
    DXGI_FORMAT format, std::uint32_t width, std::uint32_t height,
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    if (device == nullptr || IsCoreKind(kind) || !Valid(format, width, height)) return false;
    Surface* surface = Slot(kind);
    if (surface == nullptr) return false;
    if (surface->resource != nullptr && surface->format == format &&
        surface->width == width && surface->height == height)
        return true;
    if (surface->resource != nullptr &&
        retirement.Size() == NrDeferredRetirementQueue::kCapacity)
        return false;

    std::uint64_t physicalBytes = 0;
    ID3D12Resource* res = Create(device, format, width, height, &physicalBytes);
    Surface next = MakeSurface(res, format, width, height, physicalBytes);
    if (next.resource == nullptr) return false;
    if (!Park(*surface, retirement, KindToString(kind), fence, fenceValue)) {
        Release(next);
        return false;
    }
    *surface = next;
    D3D12NrAllocationTracker::Instance().RecordAllocation({
        "Scratch",
        KindToString(kind),
        format,
        width,
        height,
        LogicalBytes(next),
        next.physicalBytes,
        false,
        true
    });
    return true;
}

bool D3D12NrScratchResources::RetireUnused(
    const D3D12NrScratchUsage& usage,
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    struct Candidate {
        D3D12NrScratchKind kind;
        bool keep;
    };
    const Candidate candidates[] = {
        {D3D12NrScratchKind::PassScratch, usage.passScratch},
        {D3D12NrScratchKind::ColorSmall, usage.colorSmall},
        {D3D12NrScratchKind::OutputNative, usage.outputNative},
        {D3D12NrScratchKind::ActiveColor, usage.activeColor},
        {D3D12NrScratchKind::ResidualEdited, usage.residual},
        {D3D12NrScratchKind::ResidualHistory0, usage.residual},
        {D3D12NrScratchKind::ResidualHistory1, usage.residual},
        {D3D12NrScratchKind::ResidualComposed, usage.residualComposed},
    };

    std::size_t retireCount = 0;
    for (const auto& candidate : candidates) {
        const Surface* surface = Slot(candidate.kind);
        if (!candidate.keep && surface != nullptr && surface->resource != nullptr)
            ++retireCount;
    }
    if (retireCount > NrDeferredRetirementQueue::kCapacity - retirement.Size())
        return false;
    if (retireCount == 0) return true;

    for (const auto& candidate : candidates) {
        if (candidate.keep) continue;
        Surface* surface = Slot(candidate.kind);
        if (surface != nullptr && !Park(*surface, retirement, KindToString(candidate.kind), fence, fenceValue)) return false;
    }
    return true;
}

bool D3D12NrScratchResources::Retire(
    D3D12NrScratchKind kind, NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    Surface* surface = Slot(kind);
    if (surface == nullptr) return false;
    if (surface->resource != nullptr &&
        retirement.Size() == NrDeferredRetirementQueue::kCapacity)
        return false;
    return Park(*surface, retirement, KindToString(kind), fence, fenceValue);
}

bool D3D12NrScratchResources::Retire(
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    if (!ParkAll(retirement, fence, fenceValue)) return false;
    desc_ = {};
    return true;
}

bool D3D12NrScratchResources::QueueTransition(
    D3D12NrScratchKind kind, D3D12_RESOURCE_STATES expected,
    D3D12_RESOURCE_STATES next, D3D12_RESOURCE_BARRIER& barrier) noexcept {
    Surface* surface = Slot(kind);
    if (surface == nullptr || surface->resource == nullptr || surface->state != expected)
        return false;
    if (expected == next) return true;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = surface->resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = expected;
    barrier.Transition.StateAfter = next;
    surface->state = next;
    return true;
}

bool D3D12NrScratchResources::Transition(
    ID3D12GraphicsCommandList* cmdList, D3D12NrScratchKind kind,
    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept {
    if (cmdList == nullptr) return false;
    if (expected == next) return true;
    D3D12_RESOURCE_BARRIER barrier{};
    if (!QueueTransition(kind, expected, next, barrier)) return false;
    cmdList->ResourceBarrier(1, &barrier);
    return true;
}

bool D3D12NrScratchResources::RestoreAllToUav(ID3D12GraphicsCommandList* cmdList) noexcept {
    if (cmdList == nullptr) return false;
    D3D12_RESOURCE_BARRIER barriers[11]{};
    Surface* surfaces[11]{};
    UINT count = 0;
    for (Surface* s : {&output_, &colorCopy_, &hdrCopy_, &passScratch_,
                       &colorSmall_, &outputNative_, &activeColor_,
                       &residualEdited_, &residualHistory0_, &residualHistory1_,
                       &residualComposed_}) {
        if (s->resource == nullptr || s->state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            continue;
        barriers[count].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[count].Transition.pResource = s->resource;
        barriers[count].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[count].Transition.StateBefore = s->state;
        barriers[count].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        surfaces[count++] = s;
    }
    if (count > 0) {
        cmdList->ResourceBarrier(count, barriers);
        for (UINT i = 0; i < count; ++i)
            surfaces[i]->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    return true;
}

void D3D12NrScratchResources::ReleaseAfterIdle() noexcept {
    Release(output_);
    Release(colorCopy_);
    Release(hdrCopy_);
    Release(passScratch_);
    Release(colorSmall_);
    Release(outputNative_);
    Release(activeColor_);
    Release(residualEdited_);
    Release(residualHistory0_);
    Release(residualHistory1_);
    Release(residualComposed_);
    desc_ = {};
}

ID3D12Resource* D3D12NrScratchResources::Get(D3D12NrScratchKind kind) const noexcept {
    const Surface* surface = Slot(kind);
    return surface == nullptr ? nullptr : surface->resource;
}

D3D12_RESOURCE_STATES D3D12NrScratchResources::State(D3D12NrScratchKind kind) const noexcept {
    const Surface* surface = Slot(kind);
    return surface == nullptr ? D3D12_RESOURCE_STATE_COMMON : surface->state;
}

} // namespace nrfusion
