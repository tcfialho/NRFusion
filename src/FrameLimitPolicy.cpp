#include "nrfusion/FrameLimitPolicy.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {
namespace {
double Positive(double v) { return std::isfinite(v) && v > 0.0 ? v : 0.0; }
}

GenerationMultiplierTracker::GenerationMultiplierTracker(unsigned confirmationsRequired)
    : confirmationsRequired_(std::max(1u, confirmationsRequired)) {}

void GenerationMultiplierTracker::Reset() {
    current_ = 1.0;
    candidate_ = 1.0;
    candidateCount_ = 0;
}

double GenerationMultiplierTracker::Update(double realFrameMs, double presentFrameMs, bool fgActive) {
    if (!fgActive) {
        Reset();
        return current_;
    }

    // Missing timing is not evidence that an already-observed 3x/4x mode suddenly became 2x.
    // Use 2x only as the initial safe fallback and otherwise preserve the last confirmed mode.
    const bool validTiming = std::isfinite(realFrameMs) && std::isfinite(presentFrameMs) &&
                             realFrameMs > 0.0 && presentFrameMs > 0.0;
    if (!validTiming) {
        if (current_ < 2.0) current_ = 2.0;
        candidate_ = current_;
        candidateCount_ = 0;
        return current_;
    }

    const double observed = FrameLimitPolicy::EstimateGenerationMultiplier(realFrameMs, presentFrameMs, true);
    if (current_ < 2.0) {
        // FG is active; accepting the first observed multiplier is safer than briefly applying a
        // displayed-FPS cap in source-FPS domain. Subsequent changes are hysteretic.
        current_ = observed;
        candidate_ = observed;
        candidateCount_ = 0;
        return current_;
    }

    if (observed == current_) {
        candidate_ = current_;
        candidateCount_ = 0;
        return current_;
    }

    if (observed != candidate_) {
        candidate_ = observed;
        candidateCount_ = 1;
    } else if (candidateCount_ < confirmationsRequired_) {
        ++candidateCount_;
    }

    if (candidateCount_ >= confirmationsRequired_) {
        current_ = candidate_;
        candidateCount_ = 0;
    }
    return current_;
}

double FrameLimitPolicy::EstimateGenerationMultiplier(double realFrameMs, double presentFrameMs,
                                                       bool fgActive) {
    if (!fgActive) return 1.0;
    if (!std::isfinite(realFrameMs) || !std::isfinite(presentFrameMs) ||
        realFrameMs <= 0.0 || presentFrameMs <= 0.0)
        return 2.0;

    const double ratio = realFrameMs / presentFrameMs;
    if (!std::isfinite(ratio) || ratio < 1.35) return 2.0;

    // MFG produces an integer number of displayed frames per source frame. Round the measured
    // cadence ratio and clamp to the presentation modes supported by NR Fusion: 2x..4x.
    // This is intentionally conservative on noisy timing and must not invent unsupported 5x/6x modes.
    return std::clamp(std::round(ratio), 2.0, 4.0);
}

FrameLimitPlan FrameLimitPolicy::Resolve(double manualCapFps, double governorSourceCapFps,
                                         double generationMultiplier) {
    const double manual = Positive(manualCapFps);
    const double governor = Positive(governorSourceCapFps);
    const double multiplier = std::clamp(Positive(generationMultiplier), 1.0, 4.0);
    const double manualSource = manual > 0.0 ? manual / multiplier : 0.0;
    double effective = 0.0;
    if (manualSource > 0.0 && governor > 0.0) effective = std::min(manualSource, governor);
    else effective = std::max(manualSource, governor);
    return {effective, manualSource, multiplier,
            governor > 0.0 && (manualSource <= 0.0 || governor < manualSource)};
}

} // namespace nrfusion
