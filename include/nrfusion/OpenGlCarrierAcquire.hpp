#pragma once

#include "nrfusion/FrameContractProvider.hpp"

#include <cstdint>
#include <optional>

namespace nrfusion {

enum class OpenGlTextureTarget : std::uint8_t {
    Invalid,
    Texture2D
};

struct OpenGlInteropFacts {
    std::uintptr_t renderContext = 0;
    bool memoryObject = false;
    bool memoryObjectWin32 = false;
    bool semaphore = false;
    bool semaphoreWin32 = false;
    bool copyImage = false;

    constexpr bool Ready() const noexcept {
        return renderContext != 0 &&
               memoryObject &&
               memoryObjectWin32 &&
               semaphore &&
               semaphoreWin32 &&
               copyImage;
    }
};

struct OpenGlNativeTextureFacts {
    std::uint32_t textureId = 0;
    Resolution resolution{};
    ResourceFormat format = ResourceFormat::Unknown;
    OpenGlTextureTarget target = OpenGlTextureTarget::Invalid;
    std::uint32_t mipLevels = 0;
    std::uint32_t sampleCount = 0;

    constexpr bool Valid() const noexcept {
        return textureId != 0 &&
               resolution.Valid() &&
               format == ResourceFormat::Rgba16Float &&
               target == OpenGlTextureTarget::Texture2D &&
               mipLevels == 1 &&
               sampleCount == 1;
    }
};

struct OpenGlNativeResourceCapture {
    OpenGlNativeTextureFacts texture{};
    ResourceProvenance provenance = ResourceProvenance::Unknown;
    ResourceReliability reliability = ResourceReliability::Unknown;
};

struct OpenGlNativeAcquireInput {
    ProviderInput identity{};
    std::uint64_t resourceGeneration = 0;
    OpenGlInteropFacts interop{};
    OpenGlNativeResourceCapture color{};
    OpenGlNativeTextureFacts output{};
    Jitter jitter{};
    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

enum class OpenGlAcquireFailure : std::uint8_t {
    None,
    InvalidIdentity,
    InvalidResourceGeneration,
    MissingContext,
    MissingInteropCapability,
    InvalidColor,
    InvalidOutput,
    InvalidEvidence,
    ResolutionMismatch
};

struct OpenGlAcquireResult {
    FrameContext frame{};
    ProviderDiagnostics diagnostics{};
    OpenGlInteropFacts interop{};
    OpenGlNativeTextureFacts colorFacts{};
    OpenGlNativeTextureFacts outputFacts{};
    std::uint64_t resourceGeneration = 0;
    OpenGlAcquireFailure failure = OpenGlAcquireFailure::None;
    bool attempted = false;

    constexpr explicit operator bool() const noexcept {
        return attempted && failure == OpenGlAcquireFailure::None;
    }
};

OpenGlAcquireResult AcquireOpenGlFrame(
    const OpenGlNativeAcquireInput& input) noexcept;

std::optional<SyntheticFrameInputs> BuildOpenGlCarrierWork(
    const FrameContext& frame,
    const WorkTicket& ticket) noexcept;

} // namespace nrfusion
