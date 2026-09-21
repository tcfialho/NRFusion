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

enum class ResourceProvenance : std::uint8_t {
    Unknown,
    GameNative,
    DlssContract,
    OpticalFlow,
    ShaderEstimated,
    Generated
};

enum class ResourceReliability : std::uint8_t {
    Unknown,
    Unreliable,
    Reliable
};

enum class ResourceOwnership : std::uint8_t {
    Unknown,
    Borrowed,
    ProviderOwned,
    Shared
};

enum class ResourceLifetime : std::uint8_t {
    Unknown,
    Frame,
    UntilNextAcquire,
    Session
};

enum class MotionSource : std::uint8_t {
    Native,
    DlssContract,
    NvidiaOpticalFlow,
    ShaderEstimated,
    Zero
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

struct ResourceRef {
    std::uint64_t opaqueId = 0;
    Resolution resolution{};
    ResourceFormat format = ResourceFormat::Unknown;
    ResourceProvenance provenance = ResourceProvenance::Unknown;
    ResourceReliability reliability = ResourceReliability::Unknown;
    ResourceOwnership ownership = ResourceOwnership::Unknown;
    ResourceLifetime lifetime = ResourceLifetime::Unknown;
    FrameId sourceFrameId = 0;

    constexpr bool Valid() const noexcept {
        return opaqueId != 0 && resolution.Valid() && format != ResourceFormat::Unknown;
    }

    constexpr bool EvidenceUnspecified() const noexcept {
        return provenance == ResourceProvenance::Unknown &&
               reliability == ResourceReliability::Unknown &&
               ownership == ResourceOwnership::Unknown &&
               lifetime == ResourceLifetime::Unknown;
    }

    constexpr bool EvidenceExplicit() const noexcept {
        return provenance != ResourceProvenance::Unknown &&
               reliability != ResourceReliability::Unknown &&
               ownership != ResourceOwnership::Unknown &&
               lifetime != ResourceLifetime::Unknown;
    }

    constexpr bool EvidenceWellFormed() const noexcept {
        if (EvidenceUnspecified()) return sourceFrameId == 0;
        return EvidenceExplicit() && sourceFrameId != 0;
    }

    constexpr bool BelongsToFrame(FrameId frameId) const noexcept {
        return sourceFrameId == 0 || sourceFrameId == frameId;
    }
};

struct GameContext {
    GraphicsApi api = GraphicsApi::Unknown;
    bool is32Bit = false;
    bool nativeDlss = false;
    bool rayReconstruction = false;
    bool frameGeneration = false;
};

struct FrameContext {
    FrameId frameId = 0;
    std::uint64_t hostFrameToken = 0;
    std::uint64_t viewId = 0;
    std::uint64_t configurationGeneration = 0;

    ResourceRef color{};
    ResourceRef depth{};
    ResourceRef motionVectors{};
    ResourceRef exposure{};
    ResourceRef reactiveMask{};

    Resolution renderResolution{};
    Resolution outputResolution{};
    Jitter jitter{};

    bool hdr = false;
    bool depthReliable = false;
    MotionSource motionVectorSource = MotionSource::Zero;
    bool motionVectorsReliable = false;
    bool cameraCut = false;
    bool resetHistory = false;
    GraphicsApi api = GraphicsApi::Unknown;

    constexpr Resolution RenderSize() const noexcept { return renderResolution; }
    constexpr Resolution OutputSize() const noexcept { return outputResolution; }
    constexpr bool HasColor() const noexcept { return color.Valid(); }
    constexpr bool HasDepth() const noexcept { return depth.Valid(); }
    constexpr bool HasExposure() const noexcept { return exposure.Valid(); }
    constexpr bool HasContractDimensions() const noexcept {
        return renderResolution.Valid() && outputResolution.Valid();
    }

    constexpr bool DepthReliable() const noexcept {
        if (!depth.Valid() || !depth.EvidenceWellFormed() || !depth.BelongsToFrame(frameId))
            return false;
        if (depth.reliability != ResourceReliability::Unknown)
            return depth.reliability == ResourceReliability::Reliable;
        return depthReliable;
    }

    constexpr bool ExposureReliable() const noexcept {
        return exposure.Valid() && exposure.EvidenceWellFormed() &&
               exposure.BelongsToFrame(frameId) &&
               exposure.reliability == ResourceReliability::Reliable;
    }

    constexpr MotionSource EffectiveMotionSource() const noexcept {
        if (!motionVectors.Valid() || !motionVectors.EvidenceWellFormed() ||
            !motionVectors.BelongsToFrame(frameId))
            return MotionSource::Zero;
        switch (motionVectors.provenance) {
        case ResourceProvenance::GameNative: return MotionSource::Native;
        case ResourceProvenance::DlssContract: return MotionSource::DlssContract;
        case ResourceProvenance::OpticalFlow: return MotionSource::NvidiaOpticalFlow;
        case ResourceProvenance::ShaderEstimated: return MotionSource::ShaderEstimated;
        case ResourceProvenance::Generated: return MotionSource::Zero;
        case ResourceProvenance::Unknown: return motionVectorSource;
        }
        return MotionSource::Zero;
    }

    constexpr bool HasNativeMotion() const noexcept {
        return motionVectors.Valid() && EffectiveMotionSource() == MotionSource::Native;
    }

    constexpr bool HasDlssContractMotion() const noexcept {
        return motionVectors.Valid() && EffectiveMotionSource() == MotionSource::DlssContract;
    }

    constexpr bool MotionReliable(MotionSource source) const noexcept {
        if (!motionVectors.Valid() || !motionVectors.EvidenceWellFormed() ||
            !motionVectors.BelongsToFrame(frameId) || EffectiveMotionSource() != source)
            return false;
        if (motionVectors.reliability != ResourceReliability::Unknown)
            return motionVectors.reliability == ResourceReliability::Reliable;
        return motionVectorsReliable;
    }

    constexpr bool ResourcesBelongToFrame() const noexcept {
        return (!color.Valid() || color.BelongsToFrame(frameId)) &&
               (!depth.Valid() || depth.BelongsToFrame(frameId)) &&
               (!motionVectors.Valid() || motionVectors.BelongsToFrame(frameId)) &&
               (!exposure.Valid() || exposure.BelongsToFrame(frameId)) &&
               (!reactiveMask.Valid() || reactiveMask.BelongsToFrame(frameId));
    }

    constexpr bool ResourceEvidenceWellFormed() const noexcept {
        return (!color.Valid() || color.EvidenceWellFormed()) &&
               (!depth.Valid() || depth.EvidenceWellFormed()) &&
               (!motionVectors.Valid() || motionVectors.EvidenceWellFormed()) &&
               (!exposure.Valid() || exposure.EvidenceWellFormed()) &&
               (!reactiveMask.Valid() || reactiveMask.EvidenceWellFormed());
    }

    bool ReadyForCore() const noexcept {
        const bool colorAllowed = color.reliability != ResourceReliability::Unreliable;
        return frameId != 0 && api != GraphicsApi::Unknown && HasColor() &&
               HasContractDimensions() && color.resolution == renderResolution &&
               ResourcesBelongToFrame() && ResourceEvidenceWellFormed() && colorAllowed &&
               std::isfinite(jitter.x) && std::isfinite(jitter.y);
    }
};

} // namespace nrfusion
