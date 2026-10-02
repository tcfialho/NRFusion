#include "nrfusion/PerformanceController.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace nrfusion {
namespace {

constexpr std::size_t kNrControlWindow = 5;

double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

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
    nrWarmupSamples_.reserve(config_.nrWarmupSamples);
    nrRecentSamples_.reserve(std::max(config_.nrWarmupSamples, kNrControlWindow));
    frameRecentSamples_.reserve(kNrControlWindow);
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

void PerformanceController::ObserveScaleCost(float scale, double gpuMs) {
    costModel_.Observe(scale, gpuMs);
}

void PerformanceController::ClearLearnedCostModel() {
    costModel_.Reset();
}

std::optional<NrCostFit> PerformanceController::LearnedCostFit() const {
    return costModel_.Fit();
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


} // namespace nrfusion
