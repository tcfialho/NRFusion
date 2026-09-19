#pragma once

#include "nrfusion/Types.hpp"

#include <cstddef>
#include <vector>

namespace nrfusion {

enum class AsyncQualificationState : std::uint8_t {
    NeedSerializedBaseline,
    ReadyForTrial,
    CollectingAsyncTrial,
    PreferAsync,
    PreferSerialized
};

struct AsyncQualificationConfig {
    std::size_t baselineSamples = 30;
    std::size_t trialSamples = 30;
    double minMedianGain = 0.02;       // at least 2% critical frame-time improvement
    double maxP95Regression = 0.05;    // never buy average FPS with >5% p95 regression
    double minMeanOverlap = 0.10;
    double maxMeanQueuePressure = 0.85;
};

struct AsyncQualificationResult {
    AsyncQualificationState state = AsyncQualificationState::NeedSerializedBaseline;
    bool qualified = false;
    double serializedMedianMs = 0.0;
    double asyncMedianMs = 0.0;
    double serializedP95Ms = 0.0;
    double asyncP95Ms = 0.0;
    double medianGain = 0.0;
    double p95Regression = 0.0;
    double meanAsyncOverlap = 0.0;
    double meanAsyncQueuePressure = 0.0;
};

// Hidden per-shape benchmark used by Auto mode. It never enables async merely because a compute
// queue exists: the async path must beat serialized frame time while preserving tail latency.
class AsyncQualification {
public:
    explicit AsyncQualification(AsyncQualificationConfig config = {});

    void Reset();
    void Observe(SchedulerMode mode, const TelemetrySample& sample);

    AsyncQualificationResult Result() const;
    AsyncQualificationState State() const noexcept { return state_; }
    bool ReadyForTrial() const noexcept { return state_ == AsyncQualificationState::ReadyForTrial; }
    bool Qualified() const noexcept { return state_ == AsyncQualificationState::PreferAsync; }
    SchedulerMode NextMode(bool asyncAvailable) const noexcept;

private:
    static double Median(std::vector<double> values);
    static double Percentile95(std::vector<double> values);
    void EvaluateTrial();

    AsyncQualificationConfig config_;
    AsyncQualificationState state_ = AsyncQualificationState::NeedSerializedBaseline;
    std::vector<double> serializedFrames_;
    std::vector<double> asyncFrames_;
    double asyncOverlapSum_ = 0.0;
    double asyncQueueSum_ = 0.0;
    std::size_t asyncTelemetrySamples_ = 0;
    AsyncQualificationResult result_{};
};

} // namespace nrfusion
