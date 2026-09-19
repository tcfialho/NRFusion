#pragma once

#include "nrfusion/CrossQueueClockCalibrator.hpp"

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace nrfusion {

// Header-only bridge deliberately avoids including d3d12.h. ID3D12CommandQueue's two clock methods
// are template-dependent, so this compiles in the portable core and instantiates unchanged against
// the real COM interface in the Windows host.
template <typename QueueT>
QueueClockId D3D12QueueClockId(QueueT* queue) noexcept {
    return static_cast<QueueClockId>(reinterpret_cast<std::uintptr_t>(queue));
}

template <typename QueueT>
bool SampleD3D12QueueClock(QueueT* queue, double cpuQpcFrequencyHz,
                           QueueClockCalibrationSample& out) {
    if (queue == nullptr || !std::isfinite(cpuQpcFrequencyHz) || !(cpuQpcFrequencyHz > 0.0)) return false;

    std::uint64_t gpuFrequency = 0;
    std::uint64_t gpuTimestamp = 0;
    std::uint64_t cpuQpcTimestamp = 0;

    const auto frequencyHr = queue->GetTimestampFrequency(&gpuFrequency);
    if (static_cast<long long>(frequencyHr) < 0 || gpuFrequency == 0) return false;

    const auto calibrationHr = queue->GetClockCalibration(&gpuTimestamp, &cpuQpcTimestamp);
    if (static_cast<long long>(calibrationHr) < 0) return false;

    out.gpuTimestamp = gpuTimestamp;
    out.cpuQpcTimestamp = cpuQpcTimestamp;
    out.gpuFrequencyHz = static_cast<double>(gpuFrequency);
    out.cpuQpcFrequencyHz = cpuQpcFrequencyHz;
    return true;
}

template <typename QueueT>
bool UpdateD3D12QueueClock(CrossQueueClockCalibrator& calibrator, QueueT* queue,
                           double cpuQpcFrequencyHz) {
    QueueClockCalibrationSample sample;
    if (!SampleD3D12QueueClock(queue, cpuQpcFrequencyHz, sample)) return false;
    return calibrator.Update(D3D12QueueClockId(queue), sample);
}

} // namespace nrfusion
