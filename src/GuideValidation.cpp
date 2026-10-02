#include "nrfusion/GuideValidation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {
namespace {
double Clamp01(double v) { return std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 1.0); }
double FiniteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }
}

MotionGuideValidator::MotionGuideValidator(MotionGuideValidationConfig config) : config_(config) {
    config_.enterReliable = std::clamp(FiniteOr(config_.enterReliable, 0.72), 0.0, 1.0);
    config_.exitReliable = std::clamp(FiniteOr(config_.exitReliable, 0.45), 0.0, config_.enterReliable);
    config_.goodFramesRequired = std::max(1u, config_.goodFramesRequired);
    config_.badFramesRequired = std::max(1u, config_.badFramesRequired);
    config_.hardValidMinimum = std::clamp(FiniteOr(config_.hardValidMinimum, 0.25), 0.0, 1.0);
    config_.hardTemporalMinimum = std::clamp(FiniteOr(config_.hardTemporalMinimum, 0.15), 0.0, 1.0);
    config_.hardDepthMinimum = std::clamp(FiniteOr(config_.hardDepthMinimum, 0.20), 0.0, 1.0);
    config_.hardMagnitudeMinimum = std::clamp(FiniteOr(config_.hardMagnitudeMinimum, 0.20), 0.0, 1.0);
}

void MotionGuideValidator::Reset() {
    reliable_ = false;
    goodFrames_ = 0;
    badFrames_ = 0;
}

MotionGuideResult MotionGuideValidator::Update(const MotionGuideSample& s) {
    if (s.cameraCut || s.resetHistory || !s.present) {
        Reset();
        return {};
    }

    const double valid = Clamp01(s.validPixelRatio);
    const double temporal = Clamp01(s.temporalAgreement);
    const double depth = Clamp01(s.depthAgreement);
    const double magnitude = Clamp01(s.magnitudeSanity);

    const bool hardInvalid = valid < config_.hardValidMinimum || temporal < config_.hardTemporalMinimum ||
                             depth < config_.hardDepthMinimum || magnitude < config_.hardMagnitudeMinimum;
    // Geometric mean handles ordinary degradation; hard gates prevent a catastrophic single guide
    // dimension from being hidden by three healthy ones.
    const double confidence = hardInvalid ? 0.0 :
        std::pow(std::max(0.0, valid * temporal * depth * magnitude), 0.25);
    const bool good = !hardInvalid && confidence >= config_.enterReliable;
    const bool bad = hardInvalid || confidence < config_.exitReliable;

    if (good) {
        if (goodFrames_ != std::numeric_limits<std::uint32_t>::max()) ++goodFrames_;
        badFrames_ = 0;
    } else if (bad) {
        if (badFrames_ != std::numeric_limits<std::uint32_t>::max()) ++badFrames_;
        goodFrames_ = 0;
    } else {
        // Dead-band: preserve the state and decay counters rather than oscillating at the threshold.
        if (goodFrames_ > 0) --goodFrames_;
        if (badFrames_ > 0) --badFrames_;
    }

    if (!reliable_ && goodFrames_ >= config_.goodFramesRequired) reliable_ = true;
    if (reliable_ && badFrames_ >= config_.badFramesRequired) reliable_ = false;

    return {reliable_, confidence, goodFrames_, badFrames_};
}

StaticMotionDecision ResolveStaticMotion(bool now, bool previous) {
    if (!now) return {};
    if (previous) return {true, false};
    return {false, true};
}

} // namespace nrfusion
