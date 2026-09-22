#include "nrfusion/NrSession.hpp"

namespace nrfusion {

bool NrSession::Configure(
    const RuntimeConfig& config, const PerformanceConfig& performance) {
    if (!config.Valid()) return false;
    if (configured_) {
        if (config.generation < config_.generation) return false;
        if (config.generation == config_.generation) return config == config_;
    }

    runtime_.Reconfigure(performance);
    runtime_.BeginConfigurationEpoch(runtime_.PerformanceCfg().maxScale);
    config_ = config;
    configured_ = true;
    state_.configurationGeneration = config_.generation;
    state_.runtimeGeneration = runtime_.AutoConfigurationGeneration();
    state_.lastResolvedFrame = 0;
    return true;
}

void NrSession::Reset() noexcept {
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
    result.runtimeGeneration = runtime_.AutoConfigurationGeneration();
    result.decision = runtime_.ResolveAuto(
        packet.game, packet.frame, packet.telemetry, packet.capabilities,
        packet.requestedScheduler, packet.generationMultiplier, packet.compatibility);
    result.disposition = result.decision.supported
        ? NrSessionDisposition::Ready
        : NrSessionDisposition::Unsupported;

    state_.lastResolvedFrame = packet.frame.frameId;
    state_.runtimeGeneration = result.runtimeGeneration;
    ++state_.resolvedFrames;
    if (!result) ++state_.rejectedFrames;
    return result;
}

} // namespace nrfusion
