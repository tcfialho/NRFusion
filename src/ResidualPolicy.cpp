#include "nrfusion/ResidualPolicy.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion {

ResidualDecision ResidualPolicy::Decide(const ResidualInputs& i) const {
    if (!i.rayReconstruction) return {};
    if (i.cameraCut) return {true, false, true, 0.0};
    if (!i.hasDepth) return {true, false, true, 0.0};

    if (!std::isfinite(i.motionConfidence) || !std::isfinite(i.disocclusionRatio))
        return {true, false, true, 0.0};
    const double confidence = std::clamp(i.motionConfidence, 0.0, 1.0);
    const double disocclusion = std::clamp(i.disocclusionRatio, 0.0, 1.0);
    const double validity = confidence * (1.0 - disocclusion);
    if (validity < 0.35) return {true, false, true, 0.0};

    // Higher confidence lets the history persist; deliberately capped to avoid stale residual lock-in.
    const double historyWeight = std::clamp(0.35 + 0.55 * validity, 0.35, 0.88);
    return {true, true, false, historyWeight};
}

} // namespace nrfusion
