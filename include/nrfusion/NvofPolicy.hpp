#pragma once

#include <cstdint>

namespace nrfusion {

enum class NvofResolution : std::uint8_t {
    Auto,
    P180,
    P360,
    P720,
    P1080,
    P1440,
    Native
};

struct NvofResolutionPlan {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    NvofResolution selected = NvofResolution::Auto;
    double motionScaleX = 1.0;
    double motionScaleY = 1.0;
};

class NvofPolicy {
public:
    // Auto intentionally caps the analysis to 180p. The full-resolution motion field is reconstructed
    // afterward by the provider; only Optical Flow analysis runs at this reduced size.
    NvofResolutionPlan Resolve(std::uint32_t sourceWidth,
                               std::uint32_t sourceHeight,
                               NvofResolution requested = NvofResolution::Auto) const;
};

} // namespace nrfusion
