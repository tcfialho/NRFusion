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

enum class RuntimeMfgMode : std::uint8_t {
    FollowGame,
    Fixed,
    Dynamic,
    Off
};

enum class RuntimeMfgQuality : std::uint8_t {
    Performance,
    Enhanced
};

struct RuntimeConfig {
    std::uint64_t generation = 1;
    bool enabled = false;
    RuntimeNrMode mode = RuntimeNrMode::Auto;
    float targetFps = 60.0f;
    float displayHz = 60.0f;
    bool displayHzAuto = true;
    RuntimeMfgMode mfgMode = RuntimeMfgMode::FollowGame;
    RuntimeMfgQuality mfgQuality = RuntimeMfgQuality::Performance;
    std::uint8_t mfgMultiplier = 2;

    constexpr bool operator==(const RuntimeConfig&) const noexcept = default;

    bool Valid() const noexcept {
        const bool knownNrMode = mode == RuntimeNrMode::Auto ||
                                 mode == RuntimeNrMode::BestQuality ||
                                 mode == RuntimeNrMode::Performance ||
                                 mode == RuntimeNrMode::Custom;
        const bool knownMfgMode = mfgMode == RuntimeMfgMode::FollowGame ||
                                  mfgMode == RuntimeMfgMode::Fixed ||
                                  mfgMode == RuntimeMfgMode::Dynamic ||
                                  mfgMode == RuntimeMfgMode::Off;
        const bool knownMfgQuality =
            mfgQuality == RuntimeMfgQuality::Performance ||
            mfgQuality == RuntimeMfgQuality::Enhanced;
        const bool validMultiplier = mfgMultiplier >= 2 && mfgMultiplier <= 6;
        return generation != 0 && knownNrMode && knownMfgMode &&
               knownMfgQuality && validMultiplier &&
               std::isfinite(targetFps) && targetFps >= 1.0f &&
               targetFps <= 1000.0f && std::isfinite(displayHz) &&
               displayHz >= 1.0f && displayHz <= 1000.0f;
    }
};

} // namespace nrfusion
