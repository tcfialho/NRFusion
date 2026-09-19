#pragma once
#include "nrfusion/PerformanceController.hpp"

namespace nrfusion {

enum class PerformancePreset {
    Auto,
    Performance,
    Aggressive,
    Balanced,
    Quality
};

PerformanceConfig MakePerformanceConfig(PerformancePreset preset, double targetFps);

} // namespace nrfusion
