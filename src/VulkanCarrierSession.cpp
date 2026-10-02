#include "nrfusion/VulkanCarrierSession.hpp"

namespace nrfusion {

bool VulkanCarrierSession::Configure(
    const RuntimeConfig& config,
    const PerformanceConfig& performance) {
    return session_.Configure(config, performance);
}

void VulkanCarrierSession::Reset() noexcept {
    session_.Reset();
}

VulkanCarrierFrameResult VulkanCarrierSession::Resolve(
    const VulkanCarrierFramePacket& packet) {
    VulkanCarrierFrameResult result{};
    NrSessionFramePacket sessionPacket{};
    const std::uint64_t activeGeneration =
        session_.State().configurationGeneration;

    if (activeGeneration == 0 ||
        packet.acquire.identity.configurationGeneration !=
            activeGeneration ||
        packet.game.api != GraphicsApi::Vulkan ||
        !session_.Config().enabled) {
        sessionPacket.game = packet.game;
        sessionPacket.frame.frameId =
            packet.acquire.identity.frameId;
        sessionPacket.frame.configurationGeneration =
            packet.acquire.identity.configurationGeneration;
        result.session = session_.Resolve(sessionPacket);
        return result;
    }

    result.acquire = AcquireVulkanFrame(packet.acquire);
    if (!result.acquire) return result;

    sessionPacket.game = packet.game;
    sessionPacket.frame = result.acquire.frame;
    sessionPacket.telemetry = packet.telemetry;
    sessionPacket.capabilities = packet.capabilities;
    sessionPacket.requestedScheduler = packet.requestedScheduler;
    sessionPacket.generationMultiplier =
        packet.generationMultiplier;
    sessionPacket.compatibility = packet.compatibility;
    result.session = session_.Resolve(sessionPacket);
    return result;
}

std::optional<VulkanCarrierWork> VulkanCarrierSession::BeginWork(
    const VulkanCarrierFrameResult& frame,
    std::uint64_t viewKey) noexcept {
    if (!frame) return std::nullopt;
    const auto ticket = session_.BeginWork(frame.session, viewKey);
    if (!ticket) return std::nullopt;
    return VulkanCarrierWork{*ticket, ticket->id};
}

bool VulkanCarrierSession::CanExecuteWork(
    const VulkanCarrierWork& work) const noexcept {
    return work && session_.CanExecuteWork(work.ticket);
}

bool VulkanCarrierSession::ClaimExecuteWork(
    const VulkanCarrierWork& work) noexcept {
    return work && session_.ClaimExecuteWork(work.ticket);
}

bool VulkanCarrierSession::HasClaimedExecution(
    const VulkanCarrierWork& work) const noexcept {
    return work && session_.HasClaimedExecution(work.ticket);
}

bool VulkanCarrierSession::ConsumeClaimedExecution(
    const VulkanCarrierWork& work) noexcept {
    return work && session_.ConsumeClaimedExecution(work.ticket);
}

bool VulkanCarrierSession::SubmitWork(
    const VulkanCarrierWork& work) noexcept {
    return work && session_.SubmitWork(work.ticket);
}

bool VulkanCarrierSession::AbandonWork(
    const VulkanCarrierWork& work) noexcept {
    return work && session_.AbandonWork(work.ticket);
}

bool VulkanCarrierSession::MapTimedWork(
    const VulkanCarrierWork& work) noexcept {
    return work && session_.MapTimedWork(work.ticket);
}

bool VulkanCarrierSession::RetireTimedSample(
    const NrRetiredTimingSample& sample) {
    return session_.RetireTimedSample(sample);
}

bool VulkanCarrierSession::RetireTimedInterval(double gpuMs) {
    return session_.RetireTimedInterval(gpuMs);
}

} // namespace nrfusion
