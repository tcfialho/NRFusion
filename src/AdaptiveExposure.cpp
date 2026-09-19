#include "nrfusion/AdaptiveExposure.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

namespace {
float SanitizeConfidenceComponent(float value) {
    if (!std::isfinite(value))
        return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}
} // namespace

float ExposureConfidence::Combined() const {
    // A chain, not a sum/average: the weakest sub-signal caps the combined confidence, so an
    // invalid/absent component (already fail-closed to 0) always zeroes the result rather than
    // being diluted by three healthy ones.
    const float r = SanitizeConfidenceComponent(resourceConfidence);
    const float t = SanitizeConfidenceComponent(temporalConfidence);
    const float s = SanitizeConfidenceComponent(sceneCorrelation);
    const float m = SanitizeConfidenceComponent(mappingConfidence);
    return std::min(std::min(r, t), std::min(s, m));
}

} // namespace nrfusion
