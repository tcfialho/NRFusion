#pragma once

#include "nrfusion/AsyncOverlapEstimator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nrfusion {

using QueueClockId = std::uint64_t;

struct QueueClockCalibrationSample {
    std::uint64_t gpuTimestamp = 0;
    std::uint64_t cpuQpcTimestamp = 0;
    double gpuFrequencyHz = 0.0;
    double cpuQpcFrequencyHz = 0.0;
};

struct QueueGpuIntervalTicks {
    QueueClockId queue = 0;
    std::uint64_t startGpuTimestamp = 0;
    std::uint64_t endGpuTimestamp = 0;
};

struct QueueClockCalibrationStatus {
    bool stable = false;
    std::size_t samples = 0;
    double offsetSeconds = 0.0;
    double spreadMs = 0.0;
    double gpuFrequencyHz = 0.0;
};

struct CrossQueueClockCalibratorConfig {
    std::size_t windowSamples = 8;
    std::size_t minStableSamples = 3;
    double maxStableSpreadMs = 0.25;
    double frequencyChangeRatio = 0.001; // reset if timestamp frequency changes by >0.1%
    double offsetJumpResetMs = 2.0; // queue/device recreation or clock-domain discontinuity
};

// Converts independent D3D12 queue timestamp domains into the CPU/QPC time domain. Each backend
// calibration sample is the tuple returned by GetClockCalibration plus both timestamp frequencies.
class CrossQueueClockCalibrator {
public:
    explicit CrossQueueClockCalibrator(CrossQueueClockCalibratorConfig config = {});

    bool Update(QueueClockId queue, const QueueClockCalibrationSample& sample);
    std::optional<double> ToCommonSeconds(QueueClockId queue, std::uint64_t gpuTimestamp) const;
    std::optional<GpuInterval> ToCommonInterval(QueueClockId queue,
                                                std::uint64_t startGpuTimestamp,
                                                std::uint64_t endGpuTimestamp) const;
    QueueClockCalibrationStatus Status(QueueClockId queue) const;
    bool Stable(QueueClockId queue) const { return Status(queue).stable; }
    void Reset(QueueClockId queue);
    void Reset();

private:
    struct State {
        double gpuFrequencyHz = 0.0;
        double cpuQpcFrequencyHz = 0.0;
        std::vector<double> offsets;
        std::vector<double> scratch;
        std::size_t head = 0;
        std::size_t size = 0;
        double medianOffset = 0.0;
        double spreadMs = 0.0;
        bool stable = false;
    };

    static double Percentile(const std::vector<double>& sorted, std::size_t size, double q);
    void EnsureStorage(State& state) const;
    void ResetState(State& state) const noexcept;
    void Recompute(State& state) const;

    CrossQueueClockCalibratorConfig config_;
    std::unordered_map<QueueClockId, State> states_;
};

} // namespace nrfusion
