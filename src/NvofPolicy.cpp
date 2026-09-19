#include "nrfusion/NvofPolicy.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

NvofResolutionPlan NvofPolicy::Resolve(std::uint32_t sourceWidth,
                                       std::uint32_t sourceHeight,
                                       NvofResolution requested) const {
    NvofResolutionPlan out{};
    out.selected = requested == NvofResolution::Auto ? NvofResolution::P180 : requested;
    if (sourceWidth == 0 || sourceHeight == 0) return out;

    std::uint32_t capHeight = sourceHeight;
    switch (requested) {
    case NvofResolution::Auto:
    case NvofResolution::P180: capHeight = 180; break;
    case NvofResolution::P360: capHeight = 360; break;
    case NvofResolution::P720: capHeight = 720; break;
    case NvofResolution::P1080: capHeight = 1080; break;
    case NvofResolution::P1440: capHeight = 1440; break;
    case NvofResolution::Native: capHeight = sourceHeight; break;
    }

    capHeight = std::min(capHeight, sourceHeight);
    const double aspect = static_cast<double>(sourceWidth) / static_cast<double>(sourceHeight);
    out.height = std::max(1u, capHeight);
    out.width = std::max(1u, static_cast<std::uint32_t>(std::llround(out.height * aspect)));
    out.width = std::min(out.width, sourceWidth);
    out.motionScaleX = static_cast<double>(sourceWidth) / static_cast<double>(out.width);
    out.motionScaleY = static_cast<double>(sourceHeight) / static_cast<double>(out.height);
    return out;
}

} // namespace nrfusion
