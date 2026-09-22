#include "nrfusion/FusionRuntime.hpp"

namespace nrfusion {

std::optional<double> FusionRuntime::ObserveCalibratedOverlap(
    const QueueGpuIntervalTicks& nr,
    const std::vector<QueueGpuIntervalTicks>& concurrent,
    double dtSeconds) {
    const auto nrCommon = queueClocks_.ToCommonInterval(
        nr.queue, nr.startGpuTimestamp, nr.endGpuTimestamp);
    if (!nrCommon) return std::nullopt;

    std::vector<GpuInterval> common;
    common.reserve(concurrent.size());
    for (const auto& interval : concurrent) {
        const auto mapped = queueClocks_.ToCommonInterval(
            interval.queue, interval.startGpuTimestamp, interval.endGpuTimestamp);
        if (mapped) common.push_back(*mapped);
    }
    if (common.empty()) return std::nullopt;
    return overlap_.Update(*nrCommon, common, dtSeconds);
}

} // namespace nrfusion
