#include "nrfusion/PerformanceController.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {

void PerformanceController::MoveDown() {
    for (std::size_t i = scaleIndex_ + 1; i < config_.scaleSteps.size(); ++i) {
        if (!failedScaleSteps_[i] && config_.scaleSteps[i] >= config_.minScale - 1e-5f) {
            scaleIndex_ = i;
            return;
        }
    }
}

void PerformanceController::MoveDownToward(float desiredScale) {
    if (!CanScaleDown()) return;

    const std::size_t first = scaleIndex_ + 1;
    const std::size_t last = std::min(config_.scaleSteps.size() - 1,
                                      scaleIndex_ + config_.maxPredictiveStepDrop);
    std::optional<std::size_t> best;
    float bestDistance = std::numeric_limits<float>::max();

    for (std::size_t i = first; i <= last; ++i) {
        const float step = config_.scaleSteps[i];
        if (step < config_.minScale - 1e-5f) break;
        if (failedScaleSteps_[i]) continue;
        const float distance = std::fabs(step - desiredScale);
        if (!best || distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }

    // The predictive window can contain only quarantined rungs while a usable rung exists farther
    // down. Never land on a known-bad rung just because the predictive jump was capped.
    if (best) scaleIndex_ = *best;
    else MoveDown();
}

void PerformanceController::MoveUp() {
    if (scaleIndex_ == 0) return;
    for (std::size_t i = scaleIndex_; i-- > 0;) {
        if (!failedScaleSteps_[i] && config_.scaleSteps[i] <= config_.maxScale + 1e-5f) {
            scaleIndex_ = i;
            return;
        }
    }
}


} // namespace nrfusion
