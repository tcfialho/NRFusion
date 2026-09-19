#include "nrfusion/PerformanceController.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nrfusion {

namespace {
constexpr std::size_t kNrControlWindow = 5;

double Clamp01(double v) { return std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 1.0); }
double PositiveOrZero(double v) { return std::isfinite(v) && v > 0.0 ? v : 0.0; }
double FiniteOr(double v, double fallback) { return std::isfinite(v) ? v : fallback; }
float FiniteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

double MedianOf(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if ((values.size() & 1u) != 0u) return values[middle];
    return (values[middle - 1] + values[middle]) * 0.5;
}

// Return a robust estimate for a newly retired NR timestamp. The raw sample is retained in the
// window so a genuinely sustained change takes over; only an isolated large deviation is clipped
// to the window median. This is deliberately bounded and independent of presentation cadence.
double RobustNrEstimate(std::vector<double>& window, double sample) {
    window.push_back(sample);
    if (window.size() > kNrControlWindow)
        window.erase(window.begin());
    if (window.size() < 3) return sample;

    const double median = MedianOf(window);
    std::vector<double> deviations;
    deviations.reserve(window.size());
    for (const double value : window) deviations.push_back(std::fabs(value - median));
    const double mad = MedianOf(std::move(deviations));
    const double tolerance = std::max(median * 0.50, mad * 4.0 + 0.05);
    return std::fabs(sample - median) > tolerance ? median : sample;
}
}

PerformanceController::PerformanceController(PerformanceConfig config) : config_(std::move(config)) {
    config_.minScale = std::clamp(FiniteOr(config_.minScale, 0.50f), 0.25f, 2.0f);
    config_.maxScale = std::clamp(FiniteOr(config_.maxScale, 1.00f), config_.minScale, 2.0f);

    // Normalize before sorting: NaN breaks ordering, and two out-of-range rungs can collapse to the
    // same effective scale after clamping and otherwise trigger fake "scale changes"/rebuilds.
    std::vector<float> normalized;
    normalized.reserve(config_.scaleSteps.size());
    for (float step : config_.scaleSteps) {
        if (!std::isfinite(step)) continue;
        normalized.push_back(std::clamp(step, config_.minScale, config_.maxScale));
    }
    if (normalized.empty()) throw std::invalid_argument("scaleSteps must contain a finite value");
    std::sort(normalized.begin(), normalized.end(), std::greater<float>());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    config_.scaleSteps = std::move(normalized);
    config_.targetFps = std::clamp(FiniteOr(config_.targetFps, 120.0), 1.0, 1000.0);
    config_.nrBudgetMs = std::max(FiniteOr(config_.nrBudgetMs, 3.0), 0.1);
    config_.nrBudgetFrameFraction = std::clamp(FiniteOr(config_.nrBudgetFrameFraction, 0.30), 0.05, 0.90);
    config_.minNrBudgetMs = std::max(FiniteOr(config_.minNrBudgetMs, 0.9), 0.1);
    config_.maxNrBudgetMs = std::max(FiniteOr(config_.maxNrBudgetMs, 5.0), config_.minNrBudgetMs);
    config_.scaleDownSustainSeconds = std::max(FiniteOr(config_.scaleDownSustainSeconds, 0.75), 0.0);
    config_.scaleUpSustainSeconds = std::max(FiniteOr(config_.scaleUpSustainSeconds, 4.0), 0.0);
    config_.cooldownSeconds = std::max(FiniteOr(config_.cooldownSeconds, 2.5), 0.0);
    config_.nrWarmupSamples = std::clamp<std::size_t>(config_.nrWarmupSamples, 2, 8);
    config_.queueHigh = std::clamp(FiniteOr(config_.queueHigh, 0.78), 0.0, 1.0);
    config_.queueLow = std::clamp(FiniteOr(config_.queueLow, 0.30), 0.0, config_.queueHigh);
    config_.frameOverBudgetRatio = std::max(FiniteOr(config_.frameOverBudgetRatio, 1.03), 0.01);
    config_.frameHeadroomRatio = std::clamp(FiniteOr(config_.frameHeadroomRatio, 0.82), 0.01, config_.frameOverBudgetRatio);
    config_.frameNrMaterialRatio = std::clamp(FiniteOr(config_.frameNrMaterialRatio, 0.75), 0.0, 1.0);
    config_.nrOverBudgetRatio = std::max(FiniteOr(config_.nrOverBudgetRatio, 1.08), 0.01);
    config_.nrHeadroomRatio = std::clamp(FiniteOr(config_.nrHeadroomRatio, 0.78), 0.01, config_.nrOverBudgetRatio);
    config_.processedRatioLow = std::clamp(FiniteOr(config_.processedRatioLow, 0.90), 0.0, 1.0);
    config_.processedRatioGood = std::clamp(FiniteOr(config_.processedRatioGood, 0.98), config_.processedRatioLow, 1.5);
    config_.sourceCapSafety = std::clamp(FiniteOr(config_.sourceCapSafety, 0.97), 0.10, 1.0);
    config_.minSourceCapFps = std::max(FiniteOr(config_.minSourceCapFps, 30.0), 1.0);
    config_.governorRecoveryFpsPerSecond = std::max(FiniteOr(config_.governorRecoveryFpsPerSecond, 8.0), 0.0);
    config_.governorReleaseHeadroom = std::max(FiniteOr(config_.governorReleaseHeadroom, 1.03), 1.001);
    config_.predictiveTargetUtilization = std::clamp(FiniteOr(config_.predictiveTargetUtilization, 0.92), 0.50, 0.99);
    config_.maxPredictiveStepDrop = std::clamp<std::size_t>(config_.maxPredictiveStepDrop, 1,
                                                            config_.scaleSteps.size());
    failedScaleSteps_.assign(config_.scaleSteps.size(), false);
    Reset(config_.maxScale);
}

void PerformanceController::Reset(float initialScale) {
    initialScale = FiniteOr(initialScale, config_.maxScale);
    scaleIndex_ = FindNearestStep(std::clamp(initialScale, config_.minScale, config_.maxScale));
    overBudgetSeconds_ = 0.0;
    headroomSeconds_ = 0.0;
    cooldownRemaining_ = 0.0;
    initialized_ = false;
    awaitingNrTiming_ = true;
    nrWarmupSamples_.clear();
    nrRecentSamples_.clear();
    frameRecentSamples_.clear();
    precisionReliefSpent_ = false;
    emaNrMs_ = emaFrameMs_ = emaQueuePressure_ = 0.0;
    emaSourceFps_ = emaProcessedFps_ = emaAsyncOverlap_ = 0.0;
    governorCapFps_ = 0.0;
}

double PerformanceController::Ewma(double previous, double value, double alpha) {
    return previous + alpha * (value - previous);
}

double PerformanceController::Median(std::vector<double> values) {
    return MedianOf(std::move(values));
}

std::size_t PerformanceController::FindNearestStep(float value) const {
    // If the requested rung itself is quarantined, compatibility wins over closeness: try the next
    // higher/native rung first. This keeps Reset(failedScale) from immediately falling to an even
    // more aggressive resolution after a resource-build incompatibility.
    for (std::size_t i = 0; i < config_.scaleSteps.size(); ++i) {
        if (!failedScaleSteps_[i] || std::fabs(config_.scaleSteps[i] - value) >= 1e-4f) continue;
        for (std::size_t j = i; j-- > 0;) {
            if (!failedScaleSteps_[j] && config_.scaleSteps[j] <= config_.maxScale + 1e-5f) return j;
        }
        for (std::size_t j = i + 1; j < config_.scaleSteps.size(); ++j) {
            if (!failedScaleSteps_[j] && config_.scaleSteps[j] >= config_.minScale - 1e-5f) return j;
        }
    }

    std::size_t best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    bool found = false;
    for (std::size_t i = 0; i < config_.scaleSteps.size(); ++i) {
        if (i < failedScaleSteps_.size() && failedScaleSteps_[i]) continue;
        const float raw = config_.scaleSteps[i];
        if (raw < config_.minScale - 1e-5f || raw > config_.maxScale + 1e-5f) continue;
        const float distance = std::fabs(raw - value);
        if (!found || distance < bestDistance) {
            bestDistance = distance;
            best = i;
            found = true;
        }
    }
    if (found) return best;

    // All usable rungs were quarantined. Keep the nearest configured step so the caller can
    // surface a terminal build failure instead of indexing an invalid slot.
    for (std::size_t i = 0; i < config_.scaleSteps.size(); ++i) {
        const float distance = std::fabs(config_.scaleSteps[i] - value);
        if (distance < bestDistance) { bestDistance = distance; best = i; }
    }
    return best;
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

double PerformanceController::ResolveNrBudgetMs(double frameBudgetMs) const {
    if (!config_.automaticNrBudget) return config_.nrBudgetMs;
    return std::clamp(frameBudgetMs * config_.nrBudgetFrameFraction,
                      config_.minNrBudgetMs, config_.maxNrBudgetMs);
}

void PerformanceController::ObserveScaleCost(float scale, double gpuMs) {
    costModel_.Observe(scale, gpuMs);
}

void PerformanceController::ClearLearnedCostModel() {
    costModel_.Reset();
}

std::optional<NrCostFit> PerformanceController::LearnedCostFit() const {
    return costModel_.Fit();
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
        nrRecentSamples_.clear();
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


std::optional<float> PerformanceController::ReportScaleBuildFailure(float failedScale) {
    if (config_.scaleSteps.empty()) return std::nullopt;
    if (!std::isfinite(failedScale)) return std::nullopt;

    std::size_t failed = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < config_.scaleSteps.size(); ++i) {
        const float d = std::fabs(config_.scaleSteps[i] - failedScale);
        if (d < bestDistance) { bestDistance = d; failed = i; }
    }
    // Only quarantine a configured rung that actually failed. A custom/free-form scale may lie
    // between two preset rungs; marking the nearest preset would poison a resolution that was
    // never attempted. The adapter keeps exact custom failures separately.
    if (bestDistance >= 1e-4f) return std::nullopt;
    failedScaleSteps_[failed] = true;

    auto usable = [&](std::size_t i) {
        return !failedScaleSteps_[i] && config_.scaleSteps[i] >= config_.minScale - 1e-5f &&
               config_.scaleSteps[i] <= config_.maxScale + 1e-5f;
    };

    std::optional<std::size_t> fallback;
    // Reduced-resolution failures prefer the next higher/native-compatible rung.
    for (std::size_t i = failed; i > 0;) {
        --i;
        if (usable(i)) { fallback = i; break; }
    }
    // Do not respond to a resource-build failure by trying an even smaller/more aggressive rung.
    // Alignment/format incompatibilities are not monotonic with pixel count; the safe direction is
    // toward native. If the preset has no higher rung, the adapter can escape to 100% explicitly.
    if (!fallback) return std::nullopt;

    scaleIndex_ = *fallback;
    overBudgetSeconds_ = 0.0;
    headroomSeconds_ = 0.0;
    cooldownRemaining_ = 0.0;
    awaitingNrTiming_ = true;
    nrWarmupSamples_.clear();
    nrRecentSamples_.clear();
    emaNrMs_ = 0.0;
    return WorkingScale();
}

void PerformanceController::ClearScaleBuildFailures() {
    std::fill(failedScaleSteps_.begin(), failedScaleSteps_.end(), false);
    scaleIndex_ = FindNearestStep(WorkingScale());
}

bool PerformanceController::IsScaleBuildFailed(float scale) const {
    if (config_.scaleSteps.empty()) return false;
    if (!std::isfinite(scale)) return false;
    std::size_t nearest = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < config_.scaleSteps.size(); ++i) {
        const float d = std::fabs(config_.scaleSteps[i] - scale);
        if (d < bestDistance) { bestDistance = d; nearest = i; }
    }
    return bestDistance < 1e-4f && nearest < failedScaleSteps_.size() && failedScaleSteps_[nearest];
}

void PerformanceController::SetPrecisionReliefAvailable(bool available) noexcept {
    // Losing the cheaper precision (unsupported, or already in use) also clears the spent mark, so
    // a later episode is not denied a trade it never actually made.
    if (!available) precisionReliefSpent_ = false;
    precisionReliefAvailable_ = available;
}

float PerformanceController::WorkingScale() const noexcept {
    return std::clamp(config_.scaleSteps[scaleIndex_], config_.minScale, config_.maxScale);
}

const PerformanceConfig& PerformanceController::Config() const noexcept { return config_; }

} // namespace nrfusion
