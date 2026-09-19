#pragma once
#include "nrfusion/PipelinePolicy.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/PerformanceController.hpp"

namespace nrfusion {

struct AutoDecision {
    bool supported = false;
    PipelineDecision pipeline{};
    SchedulerMode scheduler = SchedulerMode::Serialized;
    NrPrecision precision = NrPrecision::Fp8;
    float workingScale = 1.0f;
    double sourceCapFps = 0.0;
    PresentationMode presentation = PresentationMode::None;
    unsigned generationMultiplier = 1;
    PerformanceDecision performance{};
};

} // namespace nrfusion
