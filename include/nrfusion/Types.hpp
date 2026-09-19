#pragma once

#include <cmath>
#include <cstdint>

namespace nrfusion {

using FrameId = std::uint64_t;

struct Resolution {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    constexpr bool Valid() const noexcept { return width != 0 && height != 0; }
    constexpr bool operator==(const Resolution&) const noexcept = default;
};

struct Jitter {
    float x = 0.0f;
    float y = 0.0f;
};

enum class ResourceFormat : std::uint8_t {
    Unknown,
    R8Unorm,
    R16Float,
    R32Float,
    Rg16Float,
    Rgba16Float,
    Rgba32Float,
    D32Float
};

// Portable identity for a host-owned GPU resource. Native backends translate opaqueId to their
// API-specific object; the core only needs stable identity, dimensions and semantic format.
struct ResourceRef {
    std::uint64_t opaqueId = 0;
    Resolution resolution{};
    ResourceFormat format = ResourceFormat::Unknown;
    constexpr bool Valid() const noexcept {
        return opaqueId != 0 && resolution.Valid() && format != ResourceFormat::Unknown;
    }
};

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

enum class MotionSource : std::uint8_t {
    Native,
    DlssContract,
    NvidiaOpticalFlow,
    ShaderEstimated,
    Zero
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

enum class GraphicsApi : std::uint8_t {
    Unknown,
    D3D9,
    D3D10,
    D3D11,
    D3D12,
    Vulkan,
    OpenGL
};

enum class PresentationMode : std::uint8_t {
    None,
    FrameGeneration,
    MultiFrameGeneration
};

// Identifies the clock domain behind TelemetrySample::frameGpuMs. Presentation intervals are useful
// for pacing diagnostics, but they must never be treated as GPU occupancy by the adaptive controller.
enum class FrameTimingSource : std::uint8_t {
    GpuTimestamp,
    PresentationInterval,
    Unknown
};

// Structural game/session information used before a provider acquires the current frame.
struct GameContext {
    GraphicsApi api = GraphicsApi::Unknown;
    bool is32Bit = false;
    bool nativeDlss = false;
    bool rayReconstruction = false;
    bool frameGeneration = false;
};

struct FrameContext {
    FrameId frameId = 0;

    ResourceRef color{};
    ResourceRef depth{};
    ResourceRef motionVectors{};
    ResourceRef exposure{};
    ResourceRef reactiveMask{};

    Resolution renderResolution{};
    Resolution outputResolution{};
    Jitter jitter{};

    bool hdr = false;
    // Reliability is provider evidence, not a property implied by resource presence. Default to
    // false so a partially populated contract cannot silently authorize temporal/shader paths.
    bool depthReliable = false;
    // Providers must identify motion provenance explicitly. A valid motion resource alone does not
    // imply native-engine vectors; Bridge/Synthetic paths may expose DLSS-contract vectors instead.
    MotionSource motionVectorSource = MotionSource::Zero;
    bool motionVectorsReliable = false;
    bool cameraCut = false;
    GraphicsApi api = GraphicsApi::Unknown;

    constexpr Resolution RenderSize() const noexcept { return renderResolution; }
    constexpr Resolution OutputSize() const noexcept { return outputResolution; }
    constexpr bool HasColor() const noexcept { return color.Valid(); }
    constexpr bool HasDepth() const noexcept { return depth.Valid(); }
    constexpr bool HasExposure() const noexcept { return exposure.Valid(); }
    constexpr bool HasContractDimensions() const noexcept {
        return renderResolution.Valid() && outputResolution.Valid();
    }
    constexpr bool HasNativeMotion() const noexcept {
        return motionVectors.Valid() && motionVectorSource == MotionSource::Native;
    }
    constexpr bool HasDlssContractMotion() const noexcept {
        return motionVectors.Valid() && motionVectorSource == MotionSource::DlssContract;
    }
    constexpr bool MotionReliable(MotionSource source) const noexcept {
        return motionVectors.Valid() && motionVectorSource == source && motionVectorsReliable;
    }
    bool ReadyForCore() const noexcept {
        return frameId != 0 && api != GraphicsApi::Unknown && HasColor() && HasContractDimensions() &&
               color.resolution == renderResolution && std::isfinite(jitter.x) && std::isfinite(jitter.y);
    }
};

// Optional same-workload quality verdict supplied by a host validator. Timing alone cannot establish
// that a lower-precision or reduced-resolution technique preserved the image. The reference/error
// fields are descriptive; `stable` is the host's explicit acceptance verdict for this sample.
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
    // A timing value is actionable only when it belongs to newly retired NR work. Hosts that
    // replay the last completed timestamp must clear this marker so one stall cannot become
    // sustained overload or headroom after a queue delay.
    bool nrTimingFresh = true;
    // GPU critical-path frame time when available. A presentation interval must be marked with
    // FrameTimingSource::PresentationInterval and is ignored by performance decisions.
    double frameGpuMs = 0.0;
    FrameTimingSource frameTimingSource = FrameTimingSource::GpuTimestamp;
    double sourceFps = 0.0;
    double processedFps = 0.0;
    // True queue/backpressure telemetry only. Do not substitute NR/frame ratio.
    double queuePressure = 0.0;       // 0..1
    double asyncOverlap = 0.0;        // 0..1, fraction of NR hidden off critical path
    bool asyncComputeAvailable = false;
    bool asyncComputeStable = false;
    double crossAdapterMs = 0.0;      // round trip for MGPU path
    double secondaryNrGpuMs = 0.0;
    bool secondaryGpuAvailable = false;
    bool secondaryGpuStable = false;
    bool fgEnabled = false;
};

} // namespace nrfusion
