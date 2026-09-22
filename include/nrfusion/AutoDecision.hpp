#pragma once
#include "nrfusion/PipelinePolicy.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/PerformanceController.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace nrfusion {

struct AutoStructuralIdentity {
    FrameProvider provider = FrameProvider::Unsupported;
    ProcessTransport transport = ProcessTransport::InProcess;
    GraphicsApi api = GraphicsApi::Unknown;
    NrPlacement placement = NrPlacement::Auto;
    MotionSource motion = MotionSource::Zero;
    Resolution render{};
    Resolution output{};
    bool operator==(const AutoStructuralIdentity&) const noexcept = default;
};

inline std::uint64_t NextAutoConfigurationGeneration(
    std::uint64_t current, const char* label) {
    if (current == (std::numeric_limits<std::uint64_t>::max)())
        throw std::overflow_error(std::string(label) + " namespace exhausted");
    return current + 1;
}

inline void NormalizeTelemetryForScheduler(
    TelemetrySample& sample, SchedulerMode scheduler) noexcept {
    if (scheduler == SchedulerMode::AsyncCompute) return;
    sample.asyncOverlap = 0.0;
    if (scheduler != SchedulerMode::SecondaryGpu) return;
    const double nr = std::isfinite(sample.secondaryNrGpuMs) &&
                      sample.secondaryNrGpuMs > 0.0
        ? sample.secondaryNrGpuMs : 0.0;
    const double transport = std::isfinite(sample.crossAdapterMs) &&
                             sample.crossAdapterMs >= 0.0
        ? sample.crossAdapterMs : 0.0;
    const double critical = nr + transport;
    sample.nrGpuMs = std::isfinite(critical) ? critical : 0.0;
}

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
