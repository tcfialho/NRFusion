#include "nrfusion/SchedulerPolicy.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion {
namespace {
double FiniteOr(double value, double fallback) { return std::isfinite(value) ? value : fallback; }
double NonNegative(double value) { return std::isfinite(value) && value > 0.0 ? value : 0.0; }
}

SchedulerPolicy::SchedulerPolicy(SchedulerConfig config) : config_(config) {
    config_.minAsyncOverlap = std::clamp(FiniteOr(config_.minAsyncOverlap, 0.12), 0.0, 1.0);
    config_.secondaryGpuRequiredGain =
        std::clamp(FiniteOr(config_.secondaryGpuRequiredGain, 0.15), 0.0, 0.95);
    config_.maxCrossAdapterMs = std::max(0.0, FiniteOr(config_.maxCrossAdapterMs, 2.5));
}

SchedulerMode SchedulerPolicy::Choose(const TelemetrySample& s, SchedulerMode requested) const {
    const double overlap = std::clamp(FiniteOr(s.asyncOverlap, 0.0), 0.0, 1.0);
    const double nrMs = NonNegative(s.nrGpuMs);
    const double secondaryNrMs = NonNegative(s.secondaryNrGpuMs);
    const double crossAdapterMs = NonNegative(s.crossAdapterMs);
    const bool asyncUsable = s.asyncComputeAvailable && s.asyncComputeStable;
    const bool secondaryUsable = s.secondaryGpuAvailable && s.secondaryGpuStable &&
                                 secondaryNrMs > 0.0 && std::isfinite(s.crossAdapterMs) &&
                                 s.crossAdapterMs >= 0.0 && crossAdapterMs <= config_.maxCrossAdapterMs;

    if (requested == SchedulerMode::Serialized) return SchedulerMode::Serialized;
    if (requested == SchedulerMode::AsyncCompute)
        return asyncUsable ? SchedulerMode::AsyncCompute : SchedulerMode::Serialized;
    if (requested == SchedulerMode::SecondaryGpu) {
        if (secondaryUsable) return SchedulerMode::SecondaryGpu;
        // Secondary GPU is an optimization tier, not a hard dependency. Preserve the documented
        // degradation chain by falling back to already-qualified same-GPU async before serialized.
        if (asyncUsable && overlap >= config_.minAsyncOverlap) return SchedulerMode::AsyncCompute;
        return SchedulerMode::Serialized;
    }

    const double effectiveOverlap = asyncUsable ? overlap : 0.0;
    const double sameGpuCritical = nrMs * (1.0 - effectiveOverlap);
    const double secondaryCritical = secondaryNrMs + crossAdapterMs;

    if (secondaryUsable && sameGpuCritical > 0.0 &&
        secondaryCritical <= sameGpuCritical * (1.0 - config_.secondaryGpuRequiredGain)) {
        return SchedulerMode::SecondaryGpu;
    }

    if (asyncUsable && overlap >= config_.minAsyncOverlap)
        return SchedulerMode::AsyncCompute;
    return SchedulerMode::Serialized;
}

} // namespace nrfusion
