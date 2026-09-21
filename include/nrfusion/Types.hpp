#pragma once

#include "nrfusion/FrameContract.hpp"

#include <cmath>
#include <cstdint>

namespace nrfusion {

enum class UserMode : std::uint8_t {
    Auto,
    MaxFps,
    Balanced,
    Quality,
    Custom
};

enum class SchedulerMode : std::uint8_t {
    Auto,
    Serialized,
    AsyncCompute,
    SecondaryGpu
};

enum class NrPlacement : std::uint8_t {
    Auto,
    PreSr,
    DeferredResidual,
    AcrossRr,
    PostSr
};

enum class FrameProvider : std::uint8_t {
    Native,
    Bridge,
    Synthetic,
    Unsupported
};

enum class ProcessTransport : std::uint8_t {
    InProcess,
    X86Carrier
};

enum class PresentationMode : std::uint8_t {
    None,
    FrameGeneration,
    MultiFrameGeneration
};

enum class FrameTimingSource : std::uint8_t {
    GpuTimestamp,
    PresentationInterval,
    Unknown
};

struct QualityEvidence {
    bool available = false;
    bool stable = false;
    bool comparedToReference = false;
    double temporalConfidence = 0.0;
    double maxAbsError = 0.0;
    std::uint32_t numericFaults = 0;

    bool WellFormed() const noexcept {
        return std::isfinite(temporalConfidence) && temporalConfidence >= 0.0 &&
               temporalConfidence <= 1.0 && std::isfinite(maxAbsError) && maxAbsError >= 0.0;
    }

    bool Accepted() const noexcept {
        return available && stable && numericFaults == 0 && WellFormed();
    }
};

struct TelemetrySample {
    double dtSeconds = 0.0;
    double nrGpuMs = 0.0;
    bool nrTimingFresh = true;
    double frameGpuMs = 0.0;
    FrameTimingSource frameTimingSource = FrameTimingSource::GpuTimestamp;
    double sourceFps = 0.0;
    double processedFps = 0.0;
    double queuePressure = 0.0;
    double asyncOverlap = 0.0;
    bool asyncComputeAvailable = false;
    bool asyncComputeStable = false;
    double crossAdapterMs = 0.0;
    double secondaryNrGpuMs = 0.0;
    bool secondaryGpuAvailable = false;
    bool secondaryGpuStable = false;
    bool fgEnabled = false;
};

} // namespace nrfusion
