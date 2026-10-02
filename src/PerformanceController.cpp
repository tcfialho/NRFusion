#include "nrfusion/PerformanceController.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nrfusion {

namespace {
constexpr std::size_t kNrControlWindow = 5;
constexpr std::size_t kMaxMedianSamples = 8;

double Clamp01(double v) { return std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 1.0); }
double PositiveOrZero(double v) { return std::isfinite(v) && v > 0.0 ? v : 0.0; }

double MedianOf(const double* values, std::size_t count) {
    if (count == 0 || count > kMaxMedianSamples) return 0.0;
    std::array<double, kMaxMedianSamples> sorted{};
    std::copy_n(values, count, sorted.begin());
    for (std::size_t i = 1; i < count; ++i) {
        const double value = sorted[i];
        std::size_t j = i;
        while (j > 0 && sorted[j - 1] > value) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = value;
    }
    const std::size_t middle = count / 2;
    if ((count & 1u) != 0u) return sorted[middle];
    return (sorted[middle - 1] + sorted[middle]) * 0.5;
}

// Return a robust estimate for a newly retired NR timestamp. The raw sample is retained in the
// window so a genuinely sustained change takes over; only an isolated large deviation is clipped
// to the window median. This is deliberately bounded and independent of presentation cadence.
double RobustNrEstimate(std::vector<double>& window, double sample) {
    if (window.size() >= kNrControlWindow)
        window.erase(window.begin());
    window.push_back(sample);
    if (window.size() < 3) return sample;

    const double median = MedianOf(window.data(), window.size());
    std::array<double, kNrControlWindow> deviations{};
    for (std::size_t i = 0; i < window.size(); ++i)
        deviations[i] = std::fabs(window[i] - median);
    const double mad = MedianOf(deviations.data(), window.size());
    const double tolerance = std::max(median * 0.50, mad * 4.0 + 0.05);
    return std::fabs(sample - median) > tolerance ? median : sample;
}
}

double PerformanceController::Ewma(double previous, double value, double alpha) {
    return previous + alpha * (value - previous);
}

double PerformanceController::Median(const std::vector<double>& values) {
    return MedianOf(values.data(), values.size());
}

bool PerformanceController::CanScaleDown() const {
    for (std::size_t i = scaleIndex_ + 1; i < config_.scaleSteps.size(); ++i) {
        if (!failedScaleSteps_[i] && config_.scaleSteps[i] >= config_.minScale - 1e-5f) return true;
    }
    return false;
}

bool PerformanceController::CanScaleUp() const {
    if (scaleIndex_ == 0) return false;
    for (std::size_t i = scaleIndex_; i-- > 0;) {
        if (!failedScaleSteps_[i] && config_.scaleSteps[i] <= config_.maxScale + 1e-5f) return true;
    }
    return false;
}

double PerformanceController::ResolveNrBudgetMs(double frameBudgetMs) const {
    if (!config_.automaticNrBudget) return config_.nrBudgetMs;
    return std::clamp(frameBudgetMs * config_.nrBudgetFrameFraction,
                      config_.minNrBudgetMs, config_.maxNrBudgetMs);
}

PerformanceDecision PerformanceController::Update(const TelemetrySample& sample) {
    const double dt = std::clamp(PositiveOrZero(sample.dtSeconds), 0.0, 0.25);
    const double nrMs = PositiveOrZero(sample.nrGpuMs);
    const bool hasFreshNrTiming = sample.nrTimingFresh && nrMs > 0.0;
    const bool hasGpuFrameTiming = sample.frameTimingSource == FrameTimingSource::GpuTimestamp;
    const double frameMs = hasGpuFrameTiming ? PositiveOrZero(sample.frameGpuMs) : 0.0;
    const double queue = Clamp01(sample.queuePressure);
    const double srcFps = PositiveOrZero(sample.sourceFps);
    const double processedFps = PositiveOrZero(sample.processedFps);
    const double overlap = Clamp01(sample.asyncOverlap);

    const double alpha = dt > 0.0 ? 1.0 - std::exp(-dt / 0.50) : 0.05;
    if (!initialized_) {
        emaNrMs_ = 0.0;
        emaFrameMs_ = frameMs;
        emaQueuePressure_ = queue;
        emaSourceFps_ = srcFps;
        emaProcessedFps_ = processedFps;
        emaAsyncOverlap_ = overlap;
        initialized_ = true;
    }

    if (awaitingNrTiming_) {
        if (hasFreshNrTiming) nrWarmupSamples_.push_back(nrMs);
        if (nrWarmupSamples_.size() >= config_.nrWarmupSamples) {
            emaNrMs_ = Median(nrWarmupSamples_);
            nrRecentSamples_ = nrWarmupSamples_;
            while (nrRecentSamples_.size() > kNrControlWindow)
                nrRecentSamples_.erase(nrRecentSamples_.begin());
            nrWarmupSamples_.clear();
            awaitingNrTiming_ = false;
        }
    } else {
        if (hasFreshNrTiming)
            emaNrMs_ = Ewma(emaNrMs_, RobustNrEstimate(nrRecentSamples_, nrMs), alpha);
    }

    if (frameMs > 0.0) emaFrameMs_ = Ewma(emaFrameMs_, RobustNrEstimate(frameRecentSamples_, frameMs), alpha);
    emaQueuePressure_ = Ewma(emaQueuePressure_, queue, alpha);
    if (srcFps > 0.0) emaSourceFps_ = Ewma(emaSourceFps_, srcFps, alpha);
    if (processedFps > 0.0) emaProcessedFps_ = Ewma(emaProcessedFps_, processedFps, alpha);
    emaAsyncOverlap_ = Ewma(emaAsyncOverlap_, overlap, alpha);
    cooldownRemaining_ = std::max(0.0, cooldownRemaining_ - dt);

    const double frameBudgetMs = 1000.0 / config_.targetFps;
    const double nrBudgetMs = ResolveNrBudgetMs(frameBudgetMs);
    const double effectiveNrMs = emaNrMs_ * (1.0 - Clamp01(emaAsyncOverlap_));
    const double processedRatio = emaSourceFps_ > 1.0 && emaProcessedFps_ > 0.0
        ? emaProcessedFps_ / emaSourceFps_
        : 1.0;

    const bool frameOver = emaFrameMs_ > 0.0 && emaFrameMs_ > frameBudgetMs * config_.frameOverBudgetRatio;
    const bool nrOver = effectiveNrMs > nrBudgetMs * config_.nrOverBudgetRatio;
    const bool queueOver = emaQueuePressure_ >= config_.queueHigh;
    const bool throughputOver = processedRatio < config_.processedRatioLow;
    // A completed timing is consumed once. Without a fresh value, preserve the EMA but do not let
    // the old value accumulate another overload/headroom decision.
    const bool telemetryReady = !awaitingNrTiming_ && hasFreshNrTiming;
    // Queue/throughput congestion has its own source-FPS governor below. Lowering neural resolution
    // for congestion that the NR pass did not cause is a quality regression without a causal gain.
    // A frame-time violation can justify a scale change only when NR is a material part of the
    // measured budget; otherwise it is likely raster/CPU/presentation work outside this controller.
    const bool frameCostMaterial = frameOver && effectiveNrMs > nrBudgetMs * config_.frameNrMaterialRatio;
    const bool scaleOverBudget = nrOver || frameCostMaterial;
    const bool overBudget = telemetryReady && scaleOverBudget;

    const bool frameHeadroom = emaFrameMs_ <= 0.0 || emaFrameMs_ < frameBudgetMs * config_.frameHeadroomRatio;
    // Missing NR timing is unknown, not free headroom. This prevents a cold/rebuilt feature
    // from climbing scale before the first real GPU timestamp arrives.
    const bool nrHeadroom = hasFreshNrTiming && !awaitingNrTiming_ && emaNrMs_ > 0.0 &&
                            effectiveNrMs < nrBudgetMs * config_.nrHeadroomRatio;
    const bool queueHeadroom = emaQueuePressure_ <= config_.queueLow;
    const bool throughputGood = processedRatio >= config_.processedRatioGood;
    const bool hasHeadroom = frameHeadroom && nrHeadroom && queueHeadroom && throughputGood;

    AdaptiveState state = AdaptiveState::WaitingForTiming;
    if (telemetryReady) {
        if (overBudget) state = AdaptiveState::ReducingScale;
        else if (queueOver || throughputOver) state = AdaptiveState::ExternalPressure;
        else if (hasHeadroom && CanScaleUp()) state = AdaptiveState::RecoveringQuality;
        else state = AdaptiveState::Stable;
    }

    if (!hasFreshNrTiming || awaitingNrTiming_) {
        // A telemetry gap breaks the sustained interval. Never let the last high sample trigger a
        // later downshift after a queue stall or feature rebuild.
        overBudgetSeconds_ = 0.0;
        headroomSeconds_ = 0.0;
    } else if (overBudget) {
        overBudgetSeconds_ += dt;
        headroomSeconds_ = 0.0;
    } else if (hasHeadroom) {
        headroomSeconds_ += dt;
        overBudgetSeconds_ = std::max(0.0, overBudgetSeconds_ - dt * 0.5);
    } else {
        overBudgetSeconds_ = std::max(0.0, overBudgetSeconds_ - dt * 0.5);
        headroomSeconds_ = std::max(0.0, headroomSeconds_ - dt * 0.25);
    }

    bool changed = false;
    bool wantsPrecisionRelief = false;
    const bool downshiftDue = !awaitingNrTiming_ && cooldownRemaining_ <= 0.0 &&
                              overBudgetSeconds_ >= config_.scaleDownSustainSeconds;
    if (downshiftDue && precisionReliefAvailable_ && !precisionReliefSpent_) {
        // Spend the cheaper precision before the resolution. The cooldown and the wait for a fresh
        // NR timing are the same ones a scale change uses, so the next decision is taken on the
        // cost the new precision actually produced, never on a stale measurement.
        wantsPrecisionRelief = true;
        precisionReliefSpent_ = true;
        overBudgetSeconds_ = 0.0;
        headroomSeconds_ = 0.0;
        cooldownRemaining_ = config_.cooldownSeconds;
        awaitingNrTiming_ = true;
        emaNrMs_ = 0.0;
    } else if (downshiftDue && CanScaleDown()) {
        if (config_.predictiveScaleDown && effectiveNrMs > 0.1 && scaleOverBudget) {
            double targetNrMs = nrBudgetMs * config_.predictiveTargetUtilization;
            if (frameCostMaterial && emaFrameMs_ > frameBudgetMs) {
                const double frameExcess = emaFrameMs_ - frameBudgetMs * 0.98;
                targetNrMs = std::min(targetNrMs, std::max(0.2, effectiveNrMs - frameExcess));
            }
            // Prefer a learned fixed-overhead + area model once timings from at least two scales
            // exist. The old sqrt rule assumes zero fixed cost and tends to downshift too little when
            // launch/synchronization overhead is a meaningful part of the pass.
            const double visibleFraction = std::max(0.15, 1.0 - Clamp01(emaAsyncOverlap_));
            const double targetRawNrMs = targetNrMs / visibleFraction;
            const auto learned = costModel_.PredictScaleForCost(targetRawNrMs,
                                                                 config_.minScale,
                                                                 WorkingScale());
            const double ratio = std::clamp(targetNrMs / effectiveNrMs, 0.05, 1.0);
            const float areaFallback = static_cast<float>(static_cast<double>(WorkingScale()) * std::sqrt(ratio));
            MoveDownToward(learned.value_or(areaFallback));
        } else {
            MoveDown();
        }
        overBudgetSeconds_ = 0.0;
        headroomSeconds_ = 0.0;
        cooldownRemaining_ = config_.cooldownSeconds;
        awaitingNrTiming_ = true;
        nrWarmupSamples_.clear();
        nrRecentSamples_.clear();
        frameRecentSamples_.clear();
        emaNrMs_ = 0.0;
        changed = true;
    } else if (!awaitingNrTiming_ && cooldownRemaining_ <= 0.0 && headroomSeconds_ >= config_.scaleUpSustainSeconds && CanScaleUp()) {
        // Sustained headroom ends the episode: the next time cost rises, precision may be traded
        // again before resolution.
        precisionReliefSpent_ = false;
        MoveUp();
        overBudgetSeconds_ = 0.0;
        headroomSeconds_ = 0.0;
        cooldownRemaining_ = config_.cooldownSeconds;
        awaitingNrTiming_ = true;
        nrWarmupSamples_.clear();
        nrRecentSamples_.clear();
        emaNrMs_ = 0.0;
        changed = true;
    }

    // Congestion governor: cut quickly to measured sustainable throughput, then probe capacity
    // upward slowly. Do not drop the cap immediately when pressure clears or the queue oscillates.
    const bool congested = (queueOver || throughputOver) && emaProcessedFps_ > 1.0 &&
                           emaSourceFps_ > emaProcessedFps_ * 1.03;
    if (congested) {
        const double sustainable = std::max(config_.minSourceCapFps, emaProcessedFps_ * config_.sourceCapSafety);
        governorCapFps_ = governorCapFps_ > 0.0 ? std::min(governorCapFps_, sustainable) : sustainable;
    } else if (governorCapFps_ > 0.0) {
        const double ceiling = std::max(config_.targetFps, config_.minSourceCapFps);
        if (queueHeadroom && throughputGood)
            governorCapFps_ = std::min(ceiling, governorCapFps_ + config_.governorRecoveryFpsPerSecond * dt);
        if (governorCapFps_ >= ceiling / config_.governorReleaseHeadroom) governorCapFps_ = 0.0;
    }
    const double sourceCap = governorCapFps_;

    return PerformanceDecision {
        std::clamp(config_.scaleSteps[scaleIndex_], config_.minScale, config_.maxScale),
        changed,
        wantsPrecisionRelief,
        overBudget,
        hasHeadroom,
        telemetryReady,
        hasGpuFrameTiming && emaFrameMs_ > 0.0,
        state,
        sourceCap,
        effectiveNrMs,
        nrBudgetMs
    };
}


float PerformanceController::WorkingScale() const noexcept {
    return std::clamp(config_.scaleSteps[scaleIndex_], config_.minScale, config_.maxScale);
}

const PerformanceConfig& PerformanceController::Config() const noexcept { return config_; }

} // namespace nrfusion
