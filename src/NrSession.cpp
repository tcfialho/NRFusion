#include "nrfusion/NrSession.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace nrfusion {

bool NrSession::Configure(
    const RuntimeConfig& config, const PerformanceConfig& performance) {
    if (!config.Valid()) return false;
    if (configured_) {
        if (config.generation < config_.generation) return false;
        if (config.generation == config_.generation)
            return config == config_ && performance == performanceConfig_;
    }

    if (runtime_.AutoConfigurationGeneration() ==
        (std::numeric_limits<std::uint64_t>::max)())
        return false;

    try {
        runtime_.Reconfigure(performance);
    } catch (const std::invalid_argument&) {
        return false;
    }
    runtime_.BeginConfigurationEpoch(runtime_.PerformanceCfg().maxScale);

    if (configured_) {
        works_.ResetSession();
        timings_.Reset();
    }
    config_ = config;
    performanceConfig_ = performance;
    configured_ = true;
    state_.configurationGeneration = config_.generation;
    state_.runtimeGeneration = runtime_.AutoConfigurationGeneration();
    state_.lastResolvedFrame = 0;
    return true;
}

void NrSession::Reset() noexcept {
    works_.ResetSession();
    timings_.Reset();
    config_ = {};
    performanceConfig_ = {};
    state_ = {};
    configured_ = false;
}

NrSessionFrameResult NrSession::Reject(
    const NrSessionFramePacket& packet, NrSessionDisposition disposition) noexcept {
    ++state_.rejectedFrames;
    NrSessionFrameResult result{};
    result.disposition = disposition;
    result.frameId = packet.frame.frameId;
    result.configurationGeneration = state_.configurationGeneration;
    result.runtimeGeneration = state_.runtimeGeneration;
    return result;
}

NrSessionFrameResult NrSession::Resolve(const NrSessionFramePacket& packet) {
    if (!configured_) return Reject(packet, NrSessionDisposition::NotConfigured);
    if (packet.frame.configurationGeneration != config_.generation)
        return Reject(packet, NrSessionDisposition::StaleConfiguration);
    if (!config_.enabled) return Reject(packet, NrSessionDisposition::Disabled);
    if (!packet.frame.ReadyForCore() || packet.game.api == GraphicsApi::Unknown ||
        packet.frame.api != packet.game.api)
        return Reject(packet, NrSessionDisposition::InvalidFrame);

    NrSessionFrameResult result{};
    result.frameId = packet.frame.frameId;
    result.configurationGeneration = config_.generation;
    result.decision = runtime_.ResolveAuto(
        packet.game, packet.frame, packet.telemetry, packet.capabilities,
        packet.requestedScheduler, packet.generationMultiplier, packet.compatibility);
    result.runtimeGeneration = runtime_.AutoConfigurationGeneration();
    result.disposition = result.decision.supported
        ? NrSessionDisposition::Ready
        : NrSessionDisposition::Unsupported;

    state_.lastResolvedFrame = packet.frame.frameId;
    state_.runtimeGeneration = result.runtimeGeneration;
    ++state_.resolvedFrames;
    if (!result) ++state_.rejectedFrames;
    return result;
}

std::optional<WorkTicket> NrSession::BeginWork(
    const NrSessionFrameResult& frame, std::uint64_t viewKey) noexcept {
    if (!frame || frame.configurationGeneration != config_.generation ||
        frame.runtimeGeneration != state_.runtimeGeneration)
        return std::nullopt;
    const std::uint8_t precisionTag =
        frame.decision.precision == NrPrecision::HybridNvfp4 ? 4u : 8u;
    return works_.Begin(
        frame.frameId, viewKey, frame.runtimeGeneration,
        frame.decision.workingScale, precisionTag);
}

bool NrSession::SubmitWork(const WorkTicket& ticket) noexcept {
    return ticket.configurationGeneration == state_.runtimeGeneration &&
           works_.Submit(ticket);
}

bool NrSession::AbandonWork(const WorkTicket& ticket) noexcept {
    return works_.Abandon(ticket);
}

bool NrSession::MapTimedWork(const WorkTicket& ticket) noexcept {
    if (!works_.IsSubmitted(ticket) ||
        ticket.configurationGeneration != state_.runtimeGeneration)
        return false;
    if (const auto displaced = timings_.Push(ticket))
        works_.Abandon(*displaced);
    return true;
}

void NrSession::MapInvalidTimedAttempt() noexcept {
    if (const auto displaced = timings_.PushInvalid())
        works_.Abandon(*displaced);
}

bool NrSession::RetireTimedInterval(double gpuMs) {
    const auto entry = timings_.Pop();
    if (!entry || !entry->mapsWork) return false;
    const WorkTicket ticket = entry->ticket;
    if (!works_.Complete(ticket)) return false;
    if (ticket.configurationGeneration != state_.runtimeGeneration ||
        !std::isfinite(gpuMs) || gpuMs <= 0.0 || gpuMs >= 1000.0)
        return false;
    runtime_.ObserveScaleCost(ticket.workingScale, gpuMs);
    return true;
}

} // namespace nrfusion
