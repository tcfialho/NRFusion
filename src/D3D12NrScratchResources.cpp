#include "nrfusion/D3D12NrScratchResources.hpp"

namespace nrfusion {
namespace {

bool Valid(DXGI_FORMAT format, std::uint32_t width, std::uint32_t height) noexcept {
    return format != DXGI_FORMAT_UNKNOWN && width != 0 && height != 0;
}

bool Valid(const D3D12NrScratchDesc& desc) noexcept {
    return Valid(desc.format, desc.frameWidth, desc.frameHeight) &&
           desc.workWidth != 0 && desc.workHeight != 0;
}

D3D12NrScratchResources::Surface MakeSurface(
    ID3D12Resource* resource, DXGI_FORMAT format,
    std::uint32_t width, std::uint32_t height) noexcept {
    D3D12NrScratchResources::Surface surface{};
    surface.resource = resource;
    surface.format = format;
    surface.width = width;
    surface.height = height;
    return surface;
}

} // namespace

ID3D12Resource* D3D12NrScratchResources::Create(
    ID3D12Device* device, DXGI_FORMAT format,
    std::uint32_t width, std::uint32_t height) noexcept {
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

bool D3D12NrScratchResources::Park(
    Surface& surface, NrDeferredRetirementQueue& retirement) noexcept {
    if (surface.resource == nullptr) return true;
    void* object = surface.resource;
    if (!retirement.Park(object, NrRetiredObjectKind::Resource)) return false;
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
           static_cast<std::size_t>(activeColor_.resource != nullptr);
}

bool D3D12NrScratchResources::ParkAll(NrDeferredRetirementQueue& retirement) noexcept {
    if (ActiveCount() > NrDeferredRetirementQueue::kCapacity - retirement.Size()) return false;
    return Park(output_, retirement) &&
           Park(colorCopy_, retirement) &&
           Park(hdrCopy_, retirement) &&
           Park(passScratch_, retirement) &&
           Park(colorSmall_, retirement) &&
           Park(outputNative_, retirement) &&
           Park(activeColor_, retirement);
}

bool D3D12NrScratchResources::Complete() const noexcept {
    return output_.resource != nullptr &&
           colorCopy_.resource != nullptr &&
           hdrCopy_.resource != nullptr;
}

bool D3D12NrScratchResources::Matches(const D3D12NrScratchDesc& desc) const noexcept {
    return Complete() && desc_ == desc;
}

bool D3D12NrScratchResources::Ensure(
    ID3D12Device* device, const D3D12NrScratchDesc& desc,
    NrDeferredRetirementQueue& retirement) noexcept {
    if (!Valid(desc)) return false;
    if (Matches(desc)) return true;
    if (ActiveCount() > NrDeferredRetirementQueue::kCapacity - retirement.Size()) return false;

    Surface nextOutput = MakeSurface(
        Create(device, desc.format, desc.workWidth, desc.workHeight),
        desc.format, desc.workWidth, desc.workHeight);
    Surface nextColor = MakeSurface(
        Create(device, desc.format, desc.frameWidth, desc.frameHeight),
        desc.format, desc.frameWidth, desc.frameHeight);
    Surface nextHdr = MakeSurface(
        Create(device, desc.format, desc.frameWidth, desc.frameHeight),
        desc.format, desc.frameWidth, desc.frameHeight);

    if (nextOutput.resource == nullptr ||
        nextColor.resource == nullptr ||
        nextHdr.resource == nullptr) {
        Release(nextOutput);
        Release(nextColor);
        Release(nextHdr);
        return false;
    }

    if (!ParkAll(retirement)) {
        Release(nextOutput);
        Release(nextColor);
        Release(nextHdr);
        return false;
    }

    output_ = nextOutput;
    colorCopy_ = nextColor;
    hdrCopy_ = nextHdr;
    desc_ = desc;
    return true;
}

bool D3D12NrScratchResources::EnsureOptional(
    ID3D12Device* device, D3D12NrScratchKind kind,
    DXGI_FORMAT format, std::uint32_t width, std::uint32_t height,
    NrDeferredRetirementQueue& retirement) noexcept {
    if (device == nullptr || IsCoreKind(kind) || !Valid(format, width, height)) return false;
    Surface* surface = Slot(kind);
    if (surface == nullptr) return false;
    if (surface->resource != nullptr && surface->format == format &&
        surface->width == width && surface->height == height)
        return true;
    if (surface->resource != nullptr &&
        retirement.Size() == NrDeferredRetirementQueue::kCapacity)
        return false;

    Surface next = MakeSurface(Create(device, format, width, height), format, width, height);
    if (next.resource == nullptr) return false;
    if (!Park(*surface, retirement)) {
        Release(next);
        return false;
    }
    *surface = next;
    return true;
}

bool D3D12NrScratchResources::Retire(
    D3D12NrScratchKind kind, NrDeferredRetirementQueue& retirement) noexcept {
    Surface* surface = Slot(kind);
    if (surface == nullptr) return false;
    if (surface->resource != nullptr &&
        retirement.Size() == NrDeferredRetirementQueue::kCapacity)
        return false;
    return Park(*surface, retirement);
}

bool D3D12NrScratchResources::Retire(NrDeferredRetirementQueue& retirement) noexcept {
    if (!ParkAll(retirement)) return false;
    desc_ = {};
    return true;
}

bool D3D12NrScratchResources::Transition(
    ID3D12GraphicsCommandList* cmdList, D3D12NrScratchKind kind,
    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept {
    Surface* surface = Slot(kind);
    if (cmdList == nullptr || surface == nullptr ||
        surface->resource == nullptr || surface->state != expected)
        return false;
    if (expected == next) return true;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = surface->resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = expected;
    barrier.Transition.StateAfter = next;
    cmdList->ResourceBarrier(1, &barrier);
    surface->state = next;
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
