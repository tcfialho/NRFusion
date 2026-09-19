#include "nrfusion/TemporalConfidence.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

TemporalConfidenceResult TemporalConfidence::Evaluate(const TemporalConfidenceInput& i) const {
    if (i.cameraCut) return {0.0, true, true};
    if (!i.hasDepth) return {0.0, true, false};

    // Non-finite temporal guides are never evidence for reusing history. Treating NaN as an
    // ordinary clamp input lets it propagate through std::clamp/pow and poison the temporal chain.
    if (!std::isfinite(i.motionConfidence) || !std::isfinite(i.forwardBackwardAgreement) ||
        !std::isfinite(i.depthAgreement) || !std::isfinite(i.disocclusionRatio) ||
        !std::isfinite(i.motionMagnitudePixels))
        return {0.0, true, false};

    const double motion = std::clamp(i.motionConfidence, 0.0, 1.0);
    const double fb = std::clamp(i.forwardBackwardAgreement, 0.0, 1.0);
    const double depth = std::clamp(i.depthAgreement, 0.0, 1.0);
    const double disocc = std::clamp(i.disocclusionRatio, 0.0, 1.0);

    // Fast motion deserves a small confidence penalty because reprojection errors become more visible.
    // Keep it gentle: reliable native MVs should remain useful even at high velocity.
    const double velocityPenalty = 1.0 / (1.0 + std::max(0.0, i.motionMagnitudePixels) / 192.0);

    // Geometric mean prevents one weak temporal guide from being hidden by several strong ones.
    const double guide = std::pow(std::max(0.0, motion * fb * depth), 1.0 / 3.0);
    const double confidence = std::clamp(guide * (1.0 - disocc) * (0.75 + 0.25 * velocityPenalty), 0.0, 1.0);

    return {confidence, confidence < 0.35 || disocc >= 0.55, false};
}

} // namespace nrfusion
