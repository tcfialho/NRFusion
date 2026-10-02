#include "nrfusion/D3D12NrScratchResources.hpp"
#include "nrfusion/D3D12NrAllocationTracker.hpp"

namespace nrfusion {
namespace {

bool ValidCoreDesc(const D3D12NrScratchDesc& desc) noexcept {
    return desc.format != DXGI_FORMAT_UNKNOWN && desc.frameWidth != 0 &&
           desc.frameHeight != 0 && desc.workWidth != 0 && desc.workHeight != 0;
}

} // namespace

bool D3D12NrScratchResources::Ensure(
    ID3D12Device* device, const D3D12NrScratchDesc& desc,
    NrDeferredRetirementQueue& retirement,
    ID3D12Fence* fence,
    std::uint64_t fenceValue) noexcept {
    if (!ValidCoreDesc(desc)) return false;
    if (Matches(desc)) return true;

    const auto needsReplacement = [](const Surface& surface, DXGI_FORMAT format,
                                     std::uint32_t width, std::uint32_t height) noexcept {
        return surface.resource == nullptr || surface.format != format ||
               surface.width != width || surface.height != height;
    };
    const bool replaceOutput = needsReplacement(output_, desc.format, desc.workWidth, desc.workHeight);
    const bool replaceColor = needsReplacement(colorCopy_, desc.format, desc.frameWidth, desc.frameHeight);
    const bool replaceHdr = desc.needsHdrCopy &&
        needsReplacement(hdrCopy_, desc.format, desc.frameWidth, desc.frameHeight);
    const bool retireHdr = !desc.needsHdrCopy && hdrCopy_.resource != nullptr;

    Surface nextOutput{};
    Surface nextColor{};
    Surface nextHdr{};
    std::uint64_t outputPhys = 0;
    std::uint64_t colorPhys = 0;
    std::uint64_t hdrPhys = 0;
    if (replaceOutput) {
        ID3D12Resource* res = Create(device, desc.format, desc.workWidth, desc.workHeight, &outputPhys);
        nextOutput = MakeSurface(res, desc.format, desc.workWidth, desc.workHeight, outputPhys);
    }
    if (replaceColor) {
        ID3D12Resource* res = Create(device, desc.format, desc.frameWidth, desc.frameHeight, &colorPhys);
        nextColor = MakeSurface(res, desc.format, desc.frameWidth, desc.frameHeight, colorPhys);
    }
    if (replaceHdr) {
        ID3D12Resource* res = Create(device, desc.format, desc.frameWidth, desc.frameHeight, &hdrPhys);
        nextHdr = MakeSurface(res, desc.format, desc.frameWidth, desc.frameHeight, hdrPhys);
    }

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
        static_cast<std::size_t>(replaceHdr && hdrCopy_.resource != nullptr) +
        static_cast<std::size_t>(retireHdr);
    if (retireCount > NrDeferredRetirementQueue::kCapacity - retirement.Size()) {
        Release(nextOutput);
        Release(nextColor);
        Release(nextHdr);
        return false;
    }

    if (retireHdr) {
        if (!Park(hdrCopy_, retirement, KindToString(D3D12NrScratchKind::HdrCopy), fence, fenceValue)) {
            Release(nextOutput);
            Release(nextColor);
            Release(nextHdr);
            return false;
        }
    }

    if (replaceOutput) {
        if (!Park(output_, retirement, KindToString(D3D12NrScratchKind::Output), fence, fenceValue)) return false;
        output_ = nextOutput;
        D3D12NrAllocationTracker::Instance().RecordAllocation({
            "Scratch", "Output", desc.format, desc.workWidth, desc.workHeight,
            LogicalBytes(output_), output_.physicalBytes, true, true
        });
    }
    if (replaceColor) {
        if (!Park(colorCopy_, retirement, KindToString(D3D12NrScratchKind::ColorCopy), fence, fenceValue)) return false;
        colorCopy_ = nextColor;
        D3D12NrAllocationTracker::Instance().RecordAllocation({
            "Scratch", "ColorCopy", desc.format, desc.frameWidth, desc.frameHeight,
            LogicalBytes(colorCopy_), colorCopy_.physicalBytes, true, true
        });
    }
    if (replaceHdr) {
        if (!Park(hdrCopy_, retirement, KindToString(D3D12NrScratchKind::HdrCopy), fence, fenceValue)) return false;
        hdrCopy_ = nextHdr;
        D3D12NrAllocationTracker::Instance().RecordAllocation({
            "Scratch", "HdrCopy", desc.format, desc.frameWidth, desc.frameHeight,
            LogicalBytes(hdrCopy_), hdrCopy_.physicalBytes, true, true
        });
    }
    desc_ = desc;
    return Complete();
}

} // namespace nrfusion
