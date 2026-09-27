#include "nrfusion/RuntimeAdvancedConfig.hpp"

namespace nrfusion {
namespace {

bool KnownPrecision(NrPrecision value) noexcept {
    return value == NrPrecision::Fp8 || value == NrPrecision::HybridNvfp4;
}

bool KnownStyle(Dlss5Style value) noexcept {
    return value == Dlss5Style::Default ||
           value == Dlss5Style::Natural ||
           value == Dlss5Style::Cinematic;
}

bool KnownTriState(TriState value) noexcept {
    return value == TriState::Auto ||
           value == TriState::Off ||
           value == TriState::On;
}

bool KnownExposure(ExposureSource value) noexcept {
    return value == ExposureSource::Auto ||
           value == ExposureSource::GameExposure ||
           value == ExposureSource::BufferScan ||
           value == ExposureSource::Manual;
}

bool KnownPlacement(NrPlacement value) noexcept {
    return value == NrPlacement::Auto ||
           value == NrPlacement::PreSr ||
           value == NrPlacement::DeferredResidual ||
           value == NrPlacement::AcrossRr ||
           value == NrPlacement::PostSr;
}

bool InRange(float value, float minimum, float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

bool RuntimeNrAdvancedConfig::Valid() const noexcept {
    if (!KnownPrecision(precision) ||
        !KnownStyle(appearance.style) ||
        !KnownTriState(appearance.automaticMask) ||
        !KnownExposure(exposure.mode) ||
        !KnownPlacement(placement))
        return false;

    if (!InRange(appearance.intensity, 0.0f, 2.0f) ||
        !InRange(appearance.localStructure, 0.0f, 2.0f))
        return false;

    if (appearance.skinStructure.has_value() &&
        !InRange(*appearance.skinStructure, -1.0f, 2.0f))
        return false;

    if (!InRange(exposure.minSourceConfidence, 0.0f, 1.0f) ||
        exposure.promoteSustainFrames < 1 ||
        exposure.dropSustainFrames < 1 ||
        !std::isfinite(exposure.lastStableWindowSeconds) ||
        exposure.lastStableWindowSeconds < 0.0)
        return false;

    return true;
}

} // namespace nrfusion
