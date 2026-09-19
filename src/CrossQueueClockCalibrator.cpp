#include "nrfusion/CrossQueueClockCalibrator.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nrfusion {

CrossQueueClockCalibrator::CrossQueueClockCalibrator(CrossQueueClockCalibratorConfig config)
    : config_(config) {
    config_.windowSamples = std::max<std::size_t>(3, config_.windowSamples);
    config_.minStableSamples = std::clamp<std::size_t>(config_.minStableSamples, 2, config_.windowSamples);
    config_.maxStableSpreadMs = std::max(0.001, std::isfinite(config_.maxStableSpreadMs) ? config_.maxStableSpreadMs : 0.25);
    config_.frequencyChangeRatio = std::clamp(std::isfinite(config_.frequencyChangeRatio) ? config_.frequencyChangeRatio : 0.001, 1e-6, 0.10);
    config_.offsetJumpResetMs = std::max(0.01, std::isfinite(config_.offsetJumpResetMs) ? config_.offsetJumpResetMs : 2.0);
}

double CrossQueueClockCalibrator::Percentile(std::deque<double> values, double q) {
    if (values.empty()) return 0.0;
    std::vector<double> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    q = std::clamp(q, 0.0, 1.0);
    const double pos = q * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = static_cast<std::size_t>(std::ceil(pos));
    if (lo == hi) return sorted[lo];
    const double f = pos - static_cast<double>(lo);
    return sorted[lo] * (1.0 - f) + sorted[hi] * f;
}

void CrossQueueClockCalibrator::Recompute(State& state) const {
    state.medianOffset = Percentile(state.offsets, 0.50);
    const double p10 = Percentile(state.offsets, 0.10);
    const double p90 = Percentile(state.offsets, 0.90);
    state.spreadMs = std::max(0.0, (p90 - p10) * 1000.0);
    state.stable = state.offsets.size() >= config_.minStableSamples &&
                   state.spreadMs <= config_.maxStableSpreadMs;
}

bool CrossQueueClockCalibrator::Update(QueueClockId queue,
                                       const QueueClockCalibrationSample& sample) {
    if (queue == 0 || sample.gpuFrequencyHz <= 0.0 || sample.cpuQpcFrequencyHz <= 0.0 ||
        !std::isfinite(sample.gpuFrequencyHz) || !std::isfinite(sample.cpuQpcFrequencyHz))
        return false;

    State& state = states_[queue];
    if (state.gpuFrequencyHz > 0.0) {
        const double gpuRatio = std::fabs(sample.gpuFrequencyHz - state.gpuFrequencyHz) /
                                std::max(state.gpuFrequencyHz, sample.gpuFrequencyHz);
        const double cpuRatio = state.cpuQpcFrequencyHz > 0.0
                                    ? std::fabs(sample.cpuQpcFrequencyHz - state.cpuQpcFrequencyHz) /
                                          std::max(state.cpuQpcFrequencyHz, sample.cpuQpcFrequencyHz)
                                    : 0.0;
        if (gpuRatio > config_.frequencyChangeRatio || cpuRatio > config_.frequencyChangeRatio)
            state = {};
    }

    const long double cpuSeconds = static_cast<long double>(sample.cpuQpcTimestamp) /
                                   static_cast<long double>(sample.cpuQpcFrequencyHz);
    const long double gpuSeconds = static_cast<long double>(sample.gpuTimestamp) /
                                   static_cast<long double>(sample.gpuFrequencyHz);
    const double offset = static_cast<double>(cpuSeconds - gpuSeconds);
    if (!std::isfinite(offset)) return false;

    // Queue COM pointers can be reused after device/queue recreation. Frequency alone does not
    // identify a clock epoch, so a large offset jump must discard the old calibration window.
    if (!state.offsets.empty()) {
        const double reference = state.stable ? state.medianOffset : state.offsets.back();
        if (std::fabs(offset - reference) * 1000.0 > config_.offsetJumpResetMs)
            state = {};
    }

    state.gpuFrequencyHz = sample.gpuFrequencyHz;
    state.cpuQpcFrequencyHz = sample.cpuQpcFrequencyHz;
    state.offsets.push_back(offset);
    while (state.offsets.size() > config_.windowSamples) state.offsets.pop_front();
    Recompute(state);
    return true;
}

std::optional<double> CrossQueueClockCalibrator::ToCommonSeconds(QueueClockId queue,
                                                                  std::uint64_t gpuTimestamp) const {
    const auto it = states_.find(queue);
    if (it == states_.end() || !it->second.stable || it->second.gpuFrequencyHz <= 0.0)
        return std::nullopt;
    const long double seconds = static_cast<long double>(gpuTimestamp) /
                                static_cast<long double>(it->second.gpuFrequencyHz) +
                                static_cast<long double>(it->second.medianOffset);
    const double result = static_cast<double>(seconds);
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

std::optional<GpuInterval> CrossQueueClockCalibrator::ToCommonInterval(
    QueueClockId queue, std::uint64_t startGpuTimestamp, std::uint64_t endGpuTimestamp) const {
    if (endGpuTimestamp <= startGpuTimestamp) return std::nullopt;
    const auto start = ToCommonSeconds(queue, startGpuTimestamp);
    const auto end = ToCommonSeconds(queue, endGpuTimestamp);
    if (!start || !end || *end <= *start) return std::nullopt;
    return GpuInterval{*start * 1000.0, *end * 1000.0};
}

QueueClockCalibrationStatus CrossQueueClockCalibrator::Status(QueueClockId queue) const {
    QueueClockCalibrationStatus out;
    const auto it = states_.find(queue);
    if (it == states_.end()) return out;
    out.stable = it->second.stable;
    out.samples = it->second.offsets.size();
    out.offsetSeconds = it->second.medianOffset;
    out.spreadMs = it->second.spreadMs;
    out.gpuFrequencyHz = it->second.gpuFrequencyHz;
    return out;
}

void CrossQueueClockCalibrator::Reset(QueueClockId queue) { states_.erase(queue); }
void CrossQueueClockCalibrator::Reset() { states_.clear(); }

} // namespace nrfusion
