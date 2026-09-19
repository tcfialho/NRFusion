#pragma once
#include "nrfusion/Types.hpp"

namespace nrfusion {

struct SchedulerConfig {
    double minAsyncOverlap = 0.12;
    double secondaryGpuRequiredGain = 0.15;
    double maxCrossAdapterMs = 2.5;
};

class SchedulerPolicy {
public:
    explicit SchedulerPolicy(SchedulerConfig config = {});
    SchedulerMode Choose(const TelemetrySample& sample, SchedulerMode requested = SchedulerMode::Auto) const;
private:
    SchedulerConfig config_;
};

} // namespace nrfusion
