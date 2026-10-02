#include "nrfusion/D3D12NrScratchResources.hpp"

namespace nrfusion {
namespace {

bool ValidCoreDesc(const D3D12NrScratchDesc& desc) noexcept {
    return desc.format != DXGI_FORMAT_UNKNOWN && desc.frameWidth != 0 &&
           desc.frameHeight != 0 && desc.workWidth != 0 && desc.workHeight != 0;
}

} // namespace

bool D3D12NrScratchResources::Ensure(
    ID3D12Device* device, const D3D12NrScratchDesc& desc,
    NrDeferredRetirementQueue& retirement) noexcept {
    if (!ValidCoreDesc(desc)) return false;
    if (Matches(desc)) return true;

    const auto needsReplacement = [](const Surface& surface, DXGI_FORMAT format,
                                     std::uint32_t width, std::uint32_t height) noexcept {
        return surface.resource == nullptr || surface.format != format ||
               surface.width != width || surface.height != height;
    };
    const bool replaceOutput = needsReplacement(output_, desc.format, desc.workWidth, desc.workHeight);
    const bool replaceColor = needsReplacement(colorCopy_, desc.format, desc.frameWidth, desc.frameHeight);
    const bool replaceHdr = needsReplacement(hdrCopy_, desc.format, desc.frameWidth, desc.frameHeight);

    Surface nextOutput{};
    Surface nextColor{};
    Surface nextHdr{};
    if (replaceOutput)
        nextOutput = MakeSurface(
            Create(device, desc.format, desc.workWidth, desc.workHeight),
            desc.format, desc.workWidth, desc.workHeight);
    if (replaceColor)
        nextColor = MakeSurface(
            Create(device, desc.format, desc.frameWidth, desc.frameHeight),
            desc.format, desc.frameWidth, desc.frameHeight);
    if (replaceHdr)
        nextHdr = MakeSurface(
            Create(device, desc.format, desc.frameWidth, desc.frameHeight),
            desc.format, desc.frameWidth, desc.frameHeight);

    if ((replaceOutput && nextOutput.resource == nullptr) ||
        (replaceColor && nextColor.resource == nullptr) ||
        (replaceHdr && nextHdr.resource == nullptr)) {
        Release(nextOutput);
        Release(nextColor);
        Release(nextHdr);
        return false;
    }

    const std::size_t retireCount =
        static_cast<std::size_t>(replaceOutput && output_.resource != nullptr) +
        static_cast<std::size_t>(replaceColor && colorCopy_.resource != nullptr) +
        static_cast<std::size_t>(replaceHdr && hdrCopy_.resource != nullptr);
    if (retireCount > NrDeferredRetirementQueue::kCapacity - retirement.Size()) {
        Release(nextOutput);
        Release(nextColor);
        Release(nextHdr);
        return false;
    }

    if (replaceOutput) {
        if (!Park(output_, retirement)) return false;
        output_ = nextOutput;
    }
    if (replaceColor) {
        if (!Park(colorCopy_, retirement)) return false;
        colorCopy_ = nextColor;
    }
    if (replaceHdr) {
        if (!Park(hdrCopy_, retirement)) return false;
        hdrCopy_ = nextHdr;
    }
    desc_ = desc;
    return true;
}

} // namespace nrfusion
