#include "nrfusion/AsyncQualification.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

namespace {
double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double ValidFrameMs(const TelemetrySample& sample) {
    return sample.frameTimingSource == FrameTimingSource::GpuTimestamp &&
           std::isfinite(sample.frameGpuMs) && sample.frameGpuMs > 0.0 && sample.frameGpuMs < 1000.0
        ? sample.frameGpuMs : 0.0;
}
}

AsyncQualification::AsyncQualification(AsyncQualificationConfig config) : config_(config) {
    config_.baselineSamples = std::max<std::size_t>(5, config_.baselineSamples);
    config_.trialSamples = std::max<std::size_t>(5, config_.trialSamples);
    config_.minMedianGain = std::clamp(FiniteOr(config_.minMedianGain, 0.02), 0.0, 0.50);
    config_.maxP95Regression = std::clamp(FiniteOr(config_.maxP95Regression, 0.05), 0.0, 0.50);
    config_.minMeanOverlap = std::clamp(FiniteOr(config_.minMeanOverlap, 0.10), 0.0, 1.0);
    config_.maxMeanQueuePressure = std::clamp(FiniteOr(config_.maxMeanQueuePressure, 0.85), 0.0, 1.0);
    Reset();
}

void AsyncQualification::Reset() {
    state_ = AsyncQualificationState::NeedSerializedBaseline;
    serializedFrames_.clear();
    asyncFrames_.clear();
    asyncOverlapSum_ = 0.0;
    asyncQueueSum_ = 0.0;
    asyncTelemetrySamples_ = 0;
    result_ = {};
}

double AsyncQualification::Median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    if ((n & 1u) != 0u) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) * 0.5;
}

double AsyncQualification::Percentile95(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double pos = 0.95 * static_cast<double>(values.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    const double t = pos - static_cast<double>(lo);
    return values[lo] + (values[hi] - values[lo]) * t;
}

void AsyncQualification::Observe(SchedulerMode mode, const TelemetrySample& sample) {
    const double frameMs = ValidFrameMs(sample);
    if (frameMs <= 0.0) return;

    if (state_ == AsyncQualificationState::NeedSerializedBaseline) {
        if (mode != SchedulerMode::Serialized) return;
        serializedFrames_.push_back(frameMs);
        if (serializedFrames_.size() >= config_.baselineSamples)
            state_ = AsyncQualificationState::ReadyForTrial;
        return;
    }

    // An async trial is valid only when the frame has finite, same-sample overlap/backpressure
    // telemetry. Do not let std::clamp(NaN, ...) poison the accumulated means, and do not advance
    // ReadyForTrial until a complete async sample actually arrives.
    if (mode == SchedulerMode::AsyncCompute &&
        (!std::isfinite(sample.asyncOverlap) || !std::isfinite(sample.queuePressure)))
        return;

    if (state_ == AsyncQualificationState::ReadyForTrial) {
        if (mode != SchedulerMode::AsyncCompute) return;
        state_ = AsyncQualificationState::CollectingAsyncTrial;
    }

    if (state_ != AsyncQualificationState::CollectingAsyncTrial || mode != SchedulerMode::AsyncCompute)
        return;

    asyncFrames_.push_back(frameMs);
    asyncOverlapSum_ += std::clamp(sample.asyncOverlap, 0.0, 1.0);
    asyncQueueSum_ += std::clamp(sample.queuePressure, 0.0, 1.0);
    ++asyncTelemetrySamples_;
    if (asyncFrames_.size() >= config_.trialSamples)
        EvaluateTrial();
}

void AsyncQualification::EvaluateTrial() {
    result_.serializedMedianMs = Median(serializedFrames_);
    result_.asyncMedianMs = Median(asyncFrames_);
    result_.serializedP95Ms = Percentile95(serializedFrames_);
    result_.asyncP95Ms = Percentile95(asyncFrames_);
    result_.meanAsyncOverlap = asyncTelemetrySamples_ > 0
        ? asyncOverlapSum_ / static_cast<double>(asyncTelemetrySamples_) : 0.0;
    result_.meanAsyncQueuePressure = asyncTelemetrySamples_ > 0
        ? asyncQueueSum_ / static_cast<double>(asyncTelemetrySamples_) : 1.0;

    if (result_.serializedMedianMs > 0.0)
        result_.medianGain = (result_.serializedMedianMs - result_.asyncMedianMs) /
                             result_.serializedMedianMs;
    if (result_.serializedP95Ms > 0.0)
        result_.p95Regression = (result_.asyncP95Ms - result_.serializedP95Ms) /
                                result_.serializedP95Ms;

    result_.qualified = result_.medianGain >= config_.minMedianGain &&
                        result_.p95Regression <= config_.maxP95Regression &&
                        result_.meanAsyncOverlap >= config_.minMeanOverlap &&
                        result_.meanAsyncQueuePressure <= config_.maxMeanQueuePressure;
    state_ = result_.qualified ? AsyncQualificationState::PreferAsync
                               : AsyncQualificationState::PreferSerialized;
    result_.state = state_;
}


SchedulerMode AsyncQualification::NextMode(bool asyncAvailable) const noexcept {
    if (!asyncAvailable) return SchedulerMode::Serialized;
    switch (state_) {
    case AsyncQualificationState::ReadyForTrial:
    case AsyncQualificationState::CollectingAsyncTrial:
    case AsyncQualificationState::PreferAsync:
        return SchedulerMode::AsyncCompute;
    case AsyncQualificationState::NeedSerializedBaseline:
    case AsyncQualificationState::PreferSerialized:
    default:
        return SchedulerMode::Serialized;
    }
}

AsyncQualificationResult AsyncQualification::Result() const {
    auto out = result_;
    out.state = state_;
    out.qualified = state_ == AsyncQualificationState::PreferAsync;
    return out;
}

} // namespace nrfusion
