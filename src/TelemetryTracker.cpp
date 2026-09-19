#include "nrfusion/TelemetryTracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nrfusion {

namespace {
double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double NonNegativeFinite(double value) {
    return std::isfinite(value) && value > 0.0 ? value : 0.0;
}
}

TelemetryTracker::TelemetryTracker(TelemetryTrackerConfig config) : config_(config) {
    config_.rateWindowSeconds = std::max(0.05, FiniteOr(config_.rateWindowSeconds, 0.50));
    config_.staleAfterSeconds = std::max(config_.rateWindowSeconds,
                                         FiniteOr(config_.staleAfterSeconds, 0.75));
    config_.queueCapacity = std::max<std::size_t>(1, config_.queueCapacity);
}

void TelemetryTracker::Reset() {
    sourceEvents_.clear(); processedEvents_.clear();
    sourceFps_ = processedFps_ = 0.0;
    outstanding_ = 0;
}

void TelemetryTracker::RecordEvent(std::deque<EventGroup>& events, double timestampSeconds, double& rate) {
    if (!std::isfinite(timestampSeconds)) return;
    // Monotonic callers are expected. Clamp tiny clock regressions instead of corrupting the window.
    if (!events.empty() && timestampSeconds < events.back().timestampSeconds)
        timestampSeconds = events.back().timestampSeconds;

    if (!events.empty() && std::fabs(timestampSeconds - events.back().timestampSeconds) <= 1e-9) {
        if (events.back().count != std::numeric_limits<std::size_t>::max()) ++events.back().count;
    } else
        events.push_back(EventGroup{timestampSeconds, 1});

    const double cutoff = timestampSeconds - config_.rateWindowSeconds;
    // Keep one group before the active window as a time anchor. Its events are not counted; only
    // work that happened after that anchor contributes to the measured throughput.
    while (events.size() > 2 && events[1].timestampSeconds < cutoff) events.pop_front();
    if (events.size() >= 2) {
        const double span = events.back().timestampSeconds - events.front().timestampSeconds;
        if (span > 1e-6) {
            std::size_t completedAfterAnchor = 0;
            for (std::size_t i = 1; i < events.size(); ++i) {
                if (completedAfterAnchor > std::numeric_limits<std::size_t>::max() - events[i].count) {
                    completedAfterAnchor = std::numeric_limits<std::size_t>::max();
                    break;
                }
                completedAfterAnchor += events[i].count;
            }
            if (completedAfterAnchor > 0)
                rate = static_cast<double>(completedAfterAnchor) / span;
        }
    }
}

void TelemetryTracker::OnSourceFrame(double timestampSeconds) {
    RecordEvent(sourceEvents_, timestampSeconds, sourceFps_);
}

void TelemetryTracker::OnNrSubmitted() {
    // Queue pressure is clamped separately; do not stop tracking real submissions merely because
    // backlog exceeded an arbitrary multiple of the configured queue capacity.
    if (outstanding_ < std::numeric_limits<std::size_t>::max()) ++outstanding_;
}

void TelemetryTracker::OnNrCompleted(double timestampSeconds) {
    // A completion without a matching submission is stale/duplicated telemetry. Counting it would
    // inflate processed FPS and could make the governor believe an unsustainable rate is healthy.
    if (outstanding_ == 0) return;
    RecordEvent(processedEvents_, timestampSeconds, processedFps_);
    --outstanding_;
}

void TelemetryTracker::OnNrAbandoned() {
    if (outstanding_ > 0) --outstanding_;
}

double TelemetryTracker::AgedRate(double rate, const std::deque<EventGroup>& events, double nowSeconds) const {
    if (!(rate > 0.0) || events.empty() || !std::isfinite(nowSeconds)) return rate;
    const double age = std::max(0.0, nowSeconds - events.back().timestampSeconds);
    if (age <= config_.staleAfterSeconds) return rate;
    const double decayAge = age - config_.staleAfterSeconds;
    return rate * std::exp(-decayAge / config_.rateWindowSeconds);
}

double TelemetryTracker::QueuePressure() const noexcept {
    return std::clamp(static_cast<double>(outstanding_) / static_cast<double>(config_.queueCapacity), 0.0, 1.0);
}

TelemetrySample TelemetryTracker::BuildSample(double dtSeconds, double nrGpuMs, double frameGpuMs,
                                              double asyncOverlap, double crossAdapterMs,
                                              double secondaryNrGpuMs, bool secondaryGpuAvailable,
                                              bool secondaryGpuStable, bool fgEnabled,
                                              double nowSeconds) const {
    TelemetrySample s;
    s.dtSeconds = NonNegativeFinite(dtSeconds);
    s.nrGpuMs = NonNegativeFinite(nrGpuMs);
    s.nrTimingFresh = s.nrGpuMs > 0.0;
    s.frameGpuMs = NonNegativeFinite(frameGpuMs);
    s.sourceFps = AgedRate(sourceFps_, sourceEvents_, nowSeconds);
    s.processedFps = AgedRate(processedFps_, processedEvents_, nowSeconds);
    s.queuePressure = QueuePressure();
    s.asyncOverlap = std::clamp(FiniteOr(asyncOverlap, 0.0), 0.0, 1.0);
    s.crossAdapterMs = NonNegativeFinite(crossAdapterMs);
    s.secondaryNrGpuMs = NonNegativeFinite(secondaryNrGpuMs);
    s.secondaryGpuAvailable = secondaryGpuAvailable;
    s.secondaryGpuStable = secondaryGpuStable;
    s.fgEnabled = fgEnabled;
    return s;
}

} // namespace nrfusion
