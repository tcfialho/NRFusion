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

    if (!runtime_.CanBeginConfigurationEpoch()) return false;

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
    retiredNrGpuMs_.reset();
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
    retiredNrGpuMs_.reset();
    config_ = {};
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
    TelemetrySample telemetry = packet.telemetry;
    const bool consumeRetiredTiming = retiredNrGpuMs_.has_value();
    if (consumeRetiredTiming) {
        telemetry.nrGpuMs = *retiredNrGpuMs_;
        telemetry.nrTimingFresh = true;
    }
    result.decision = runtime_.ResolveAuto(
        packet.game, packet.frame, telemetry, packet.capabilities,
        packet.requestedScheduler, packet.generationMultiplier, packet.compatibility);
    if (consumeRetiredTiming) retiredNrGpuMs_.reset();
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

bool NrSession::CanExecuteWork(const WorkTicket& ticket) const noexcept {
    return ticket.configurationGeneration == state_.runtimeGeneration &&
           works_.IsStarted(ticket);
}

bool NrSession::ClaimExecuteWork(const WorkTicket& ticket) noexcept {
    return ticket.configurationGeneration == state_.runtimeGeneration &&
           works_.ClaimExecution(ticket);
}

bool NrSession::SubmitWork(const WorkTicket& ticket) noexcept {
    return ticket.configurationGeneration == state_.runtimeGeneration &&
           works_.Submit(ticket);
}

bool NrSession::AbandonWork(const WorkTicket& ticket) noexcept {
    return works_.Abandon(ticket);
}

bool NrSession::MapTimedWork(const WorkTicket& ticket) noexcept {
    if (ticket.configurationGeneration != state_.runtimeGeneration ||
        !works_.MarkTimingMapped(ticket))
        return false;
    if (const auto displaced = timings_.Push(ticket))
        works_.Abandon(*displaced);
    return true;
}

void NrSession::MapInvalidTimedAttempt() noexcept {
    if (const auto displaced = timings_.PushInvalid())
        works_.Abandon(*displaced);
}

bool NrSession::RetireTimedSample(const NrRetiredTimingSample& sample) {
    const auto entry = timings_.Peek();
    if (!entry || entry->mapsWork != sample.mapsWork) return false;
    if (entry->mapsWork && !(entry->ticket == sample.ticket)) return false;
    timings_.Pop();
    if (!entry->mapsWork) return false;

    const WorkTicket ticket = entry->ticket;
    if (!works_.Complete(ticket)) return false;
    if (ticket.configurationGeneration != state_.runtimeGeneration ||
        !std::isfinite(sample.gpuMs) || sample.gpuMs <= 0.0 || sample.gpuMs >= 1000.0)
        return false;
    const NrPrecision precision = ticket.precisionTag == 4u
        ? NrPrecision::HybridNvfp4 : NrPrecision::Fp8;
    runtime_.ObservePrecisionCost(
        precision, ticket.configurationGeneration, sample.gpuMs);
    runtime_.ObserveScaleCost(ticket.workingScale, sample.gpuMs);
    retiredNrGpuMs_ = sample.gpuMs;
    return true;
}

bool NrSession::RetireTimedInterval(double gpuMs) {
    const auto entry = timings_.Peek();
    if (!entry) return false;
    return RetireTimedSample({entry->mapsWork, entry->ticket, gpuMs});
}

} // namespace nrfusion
