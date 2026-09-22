#pragma once

#include "nrfusion/AutoDecision.hpp"
#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/RuntimeConfig.hpp"

#include <cstdint>

namespace nrfusion {

struct CompatibilityOverride;

enum class NrSessionDisposition : std::uint8_t {
    Ready,
    Disabled,
    NotConfigured,
    StaleConfiguration,
    InvalidFrame,
    Unsupported
};

struct NrSessionFramePacket {
    GameContext game{};
    FrameContext frame{};
    TelemetrySample telemetry{};
    RuntimeCapabilities capabilities{};
    SchedulerMode requestedScheduler = SchedulerMode::Auto;
    unsigned generationMultiplier = 1;
    const CompatibilityOverride* compatibility = nullptr;
};

struct NrSessionFrameResult {
    NrSessionDisposition disposition = NrSessionDisposition::NotConfigured;
    AutoDecision decision{};
    FrameId frameId = 0;
    std::uint64_t configurationGeneration = 0;
    std::uint64_t runtimeGeneration = 0;

    constexpr explicit operator bool() const noexcept {
        return disposition == NrSessionDisposition::Ready;
    }
};

struct NrSessionState {
    std::uint64_t configurationGeneration = 0;
    std::uint64_t runtimeGeneration = 0;
    FrameId lastResolvedFrame = 0;
    std::uint64_t resolvedFrames = 0;
    std::uint64_t rejectedFrames = 0;
};

} // namespace nrfusion
