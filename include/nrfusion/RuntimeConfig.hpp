#pragma once

#include <cmath>
#include <cstdint>

namespace nrfusion {

enum class RuntimeNrMode : std::uint8_t {
    Auto,
    BestQuality,
    Performance,
    Custom
};

struct RuntimeConfig {
    std::uint64_t generation = 1;
    bool enabled = true;
    RuntimeNrMode mode = RuntimeNrMode::Auto;
    float targetFps = 60.0f;

    constexpr bool operator==(const RuntimeConfig&) const noexcept = default;

    bool Valid() const noexcept {
        const bool knownMode = mode == RuntimeNrMode::Auto ||
                               mode == RuntimeNrMode::BestQuality ||
                               mode == RuntimeNrMode::Performance ||
                               mode == RuntimeNrMode::Custom;
        return generation != 0 && knownMode && std::isfinite(targetFps) &&
               targetFps >= 1.0f && targetFps <= 1000.0f;
    }
};

} // namespace nrfusion
