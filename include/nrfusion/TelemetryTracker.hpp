#pragma once

#include "nrfusion/Types.hpp"

#include <cstddef>
#include <deque>
#include <limits>

namespace nrfusion {

struct TelemetryTrackerConfig {
    double rateWindowSeconds = 0.50;
    double staleAfterSeconds = 0.75;
    std::size_t queueCapacity = 3;
};

// Converts real workload events into throughput/backpressure fields. Timestamps are caller-provided
// monotonic seconds, allowing multi-view bursts at the same timestamp without losing event count.
class TelemetryTracker {
public:
    explicit TelemetryTracker(TelemetryTrackerConfig config = {});

    void Reset();
    void OnSourceFrame(double timestampSeconds); // compatibility alias: one neural workload requested
    void OnSourceWork(double timestampSeconds) { OnSourceFrame(timestampSeconds); }
    void OnNrSubmitted();
    void OnNrCompleted(double timestampSeconds);
    void OnNrAbandoned();

    TelemetrySample BuildSample(double dtSeconds,
                                double nrGpuMs,
                                double frameGpuMs,
                                double asyncOverlap = 0.0,
                                double crossAdapterMs = 0.0,
                                double secondaryNrGpuMs = 0.0,
                                bool secondaryGpuAvailable = false,
                                bool secondaryGpuStable = false,
                                bool fgEnabled = false,
                                double nowSeconds = std::numeric_limits<double>::quiet_NaN()) const;

    double SourceFps() const noexcept { return sourceFps_; }
    double ProcessedFps() const noexcept { return processedFps_; }
    double CurrentSourceFps(double nowSeconds) const { return AgedRate(sourceFps_, sourceEvents_, nowSeconds); }
    double CurrentProcessedFps(double nowSeconds) const { return AgedRate(processedFps_, processedEvents_, nowSeconds); }
    double QueuePressure() const noexcept;
    std::size_t Outstanding() const noexcept { return outstanding_; }

private:
    struct EventGroup {
        double timestampSeconds = 0.0;
        std::size_t count = 0;
    };
    void RecordEvent(std::deque<EventGroup>& events, std::size_t& eventsAfterAnchor,
                     double timestampSeconds, double& rate);
    double AgedRate(double rate, const std::deque<EventGroup>& events, double nowSeconds) const;

    TelemetryTrackerConfig config_;
    std::deque<EventGroup> sourceEvents_;
    std::deque<EventGroup> processedEvents_;
    double sourceFps_ = 0.0;
    double processedFps_ = 0.0;
    std::size_t sourceEventsAfterAnchor_ = 0;
    std::size_t processedEventsAfterAnchor_ = 0;
    std::size_t outstanding_ = 0;
};

} // namespace nrfusion
