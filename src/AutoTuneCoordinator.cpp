#include "nrfusion/AutoTuneCoordinator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {
namespace {

double FinitePositive(double value) noexcept {
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

double Clamp01(double value) noexcept {
    return std::clamp(std::isfinite(value) ? value : 1.0, 0.0, 1.0);
}

} // namespace

AutoTuneCoordinator::AutoTuneCoordinator(AutoTuneConfig config) : config_(std::move(config)) {
    if (!std::isfinite(config_.targetFps) || config_.targetFps < 1.0) config_.targetFps = 120.0;
    config_.targetFps = std::min(config_.targetFps, 1000.0);
    if (config_.warmupSamples == 0) config_.warmupSamples = 1;
    if (config_.measureSamples == 0) config_.measureSamples = 1;
    if (!std::isfinite(config_.maxQueuePressure)) config_.maxQueuePressure = 0.78;
    config_.maxQueuePressure = std::clamp(config_.maxQueuePressure, 0.0, 1.0);
    if (!std::isfinite(config_.frameBudgetTolerance) || config_.frameBudgetTolerance < 1.0)
        config_.frameBudgetTolerance = 1.03;

    std::vector<float> normalized;
    normalized.reserve(config_.scaleSteps.size());
    for (float scale : config_.scaleSteps) {
        if (!std::isfinite(scale)) continue;
        normalized.push_back(std::clamp(scale, 0.25f, 2.0f));
    }
    std::sort(normalized.begin(), normalized.end(), std::greater<float>());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    config_.scaleSteps = std::move(normalized);
}

void AutoTuneCoordinator::Start(const RuntimeCapabilities& caps, float minScale, float maxScale) {
    Reset();
    if (!std::isfinite(minScale)) minScale = 0.42f;
    if (!std::isfinite(maxScale)) maxScale = 1.0f;
    minScale = std::clamp(minScale, 0.25f, 2.0f);
    maxScale = std::clamp(maxScale, 0.25f, 2.0f);
    if (minScale > maxScale) std::swap(minScale, maxScale);

    if (!caps.fp8) {
        state_ = AutoTuneState::Finished;
        return;
    }

    std::vector<SchedulerMode> schedulers {SchedulerMode::Serialized};
    if (caps.asyncCompute) schedulers.push_back(SchedulerMode::AsyncCompute);
    std::vector<NrPrecision> precisions {NrPrecision::Fp8};
    if (caps.hybridNvfp4) precisions.push_back(NrPrecision::HybridNvfp4);

    for (const float scale : config_.scaleSteps) {
        if (!std::isfinite(scale) || scale < minScale || scale > maxScale) continue;
        for (const SchedulerMode scheduler : schedulers)
            for (const NrPrecision precision : precisions)
                candidates_.push_back({scale, scheduler, precision});
    }

    if (candidates_.empty()) {
        state_ = AutoTuneState::Finished;
        return;
    }
    state_ = AutoTuneState::Warmup;
}

void AutoTuneCoordinator::Observe(const TelemetrySample& sample) {
    if (state_ != AutoTuneState::Warmup && state_ != AutoTuneState::Measure) return;
    const double frameMs = sample.frameTimingSource == FrameTimingSource::GpuTimestamp
        ? FinitePositive(sample.frameGpuMs) : 0.0;
    const double nrMs = FinitePositive(sample.nrGpuMs);
    if (frameMs == 0.0 || nrMs == 0.0) return;

    if (state_ == AutoTuneState::Warmup) {
        if (++warmupSeen_ >= config_.warmupSamples) state_ = AutoTuneState::Measure;
        return;
    }

    frameMs_.push_back(frameMs);
    nrMs_.push_back(nrMs);
    queuePressure_.push_back(Clamp01(sample.queuePressure));
    if (frameMs_.size() >= config_.measureSamples) FinishCurrent(false);
}

void AutoTuneCoordinator::RejectCurrent() {
    if (state_ == AutoTuneState::Warmup || state_ == AutoTuneState::Measure)
        FinishCurrent(true);
}

void AutoTuneCoordinator::Reset() {
    state_ = AutoTuneState::Idle;
    candidates_.clear();
    results_.clear();
    candidateIndex_ = 0;
    warmupSeen_ = 0;
    frameMs_.clear();
    nrMs_.clear();
    queuePressure_.clear();
}

std::optional<AutoTuneCandidate> AutoTuneCoordinator::Current() const {
    if ((state_ == AutoTuneState::Warmup || state_ == AutoTuneState::Measure) &&
        candidateIndex_ < candidates_.size())
        return candidates_[candidateIndex_];
    return std::nullopt;
}

double AutoTuneCoordinator::Percentile(std::vector<double> values, double q) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(q, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    if (lower == upper) return values[lower];
    const double t = position - static_cast<double>(lower);
    return values[lower] * (1.0 - t) + values[upper] * t;
}

void AutoTuneCoordinator::FinishCurrent(bool rejected) {
    if (candidateIndex_ >= candidates_.size()) {
        state_ = AutoTuneState::Finished;
        return;
    }

    AutoTuneMeasurement measurement;
    measurement.candidate = candidates_[candidateIndex_];
    measurement.rejected = rejected;
    if (!rejected && !frameMs_.empty()) {
        measurement.medianFrameMs = Percentile(frameMs_, 0.50);
        measurement.p95FrameMs = Percentile(frameMs_, 0.95);
        measurement.medianNrMs = Percentile(nrMs_, 0.50);
        double pressure = 0.0;
        for (const double value : queuePressure_) pressure += value;
        measurement.meanQueuePressure = queuePressure_.empty()
                                            ? 1.0
                                            : pressure / static_cast<double>(queuePressure_.size());
        const double frameBudgetMs = 1000.0 / config_.targetFps;
        measurement.targetMet = measurement.p95FrameMs <= frameBudgetMs * config_.frameBudgetTolerance &&
                                measurement.meanQueuePressure <= config_.maxQueuePressure;
    }
    results_.push_back(measurement);
    Advance();
}

void AutoTuneCoordinator::Advance() {
    ++candidateIndex_;
    warmupSeen_ = 0;
    frameMs_.clear();
    nrMs_.clear();
    queuePressure_.clear();
    state_ = candidateIndex_ < candidates_.size() ? AutoTuneState::Warmup : AutoTuneState::Finished;
}

std::optional<std::size_t> AutoTuneCoordinator::BestIndex() const {
    if (results_.empty()) return std::nullopt;
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < results_.size(); ++i) {
        const auto& candidate = results_[i];
        if (candidate.rejected || candidate.medianFrameMs <= 0.0) continue;
        if (!best) {
            best = i;
            continue;
        }
        const auto& current = results_[*best];
        if (config_.objective == AutoTuneObjective::HighestQualityAtTarget) {
            if (candidate.targetMet != current.targetMet) {
                if (candidate.targetMet) best = i;
                continue;
            }
            if (candidate.targetMet && candidate.candidate.workingScale != current.candidate.workingScale) {
                if (candidate.candidate.workingScale > current.candidate.workingScale) best = i;
                continue;
            }
            if (candidate.targetMet && candidate.candidate.precision != current.candidate.precision) {
                // At equal spatial quality, FP8 is the quality baseline. Hybrid NVFP4 is selected
                // only when the higher-precision candidate cannot satisfy the target or when the
                // objective explicitly asks for the lowest critical path.
                if (candidate.candidate.precision == NrPrecision::Fp8) best = i;
                continue;
            }
        }
        if (candidate.p95FrameMs < current.p95FrameMs ||
            (candidate.p95FrameMs == current.p95FrameMs && candidate.medianNrMs < current.medianNrMs))
            best = i;
    }
    return best;
}

std::optional<AutoTuneMeasurement> AutoTuneCoordinator::Best() const {
    const auto index = BestIndex();
    return index ? std::optional<AutoTuneMeasurement>{results_[*index]} : std::nullopt;
}

} // namespace nrfusion
