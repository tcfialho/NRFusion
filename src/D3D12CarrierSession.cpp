#include "nrfusion/D3D12CarrierSession.hpp"

namespace nrfusion {

bool D3D12CarrierSession::Configure(
    const RuntimeConfig& config, const PerformanceConfig& performance) {
    return session_.Configure(config, performance);
}

void D3D12CarrierSession::Reset() noexcept {
    session_.Reset();
}

D3D12CarrierFrameResult D3D12CarrierSession::Resolve(
    const D3D12CarrierFramePacket& packet) {
    D3D12CarrierFrameResult result{};
    NrSessionFramePacket sessionPacket{};
    const std::uint64_t activeGeneration = session_.State().configurationGeneration;
    if (activeGeneration == 0 ||
        packet.acquire.identity.configurationGeneration != activeGeneration ||
        packet.game.api != GraphicsApi::D3D12 ||
        !session_.Config().enabled) {
        sessionPacket.game = packet.game;
        sessionPacket.frame.frameId = packet.acquire.identity.frameId;
        sessionPacket.frame.configurationGeneration =
            packet.acquire.identity.configurationGeneration;
        result.session = session_.Resolve(sessionPacket);
        return result;
    }

    result.acquire = BuildD3D12FrameContract(packet.acquire);
    if (!result.acquire) return result;

    sessionPacket.game = packet.game;
    sessionPacket.frame = result.acquire.frame;
    sessionPacket.telemetry = packet.telemetry;
    sessionPacket.capabilities = packet.capabilities;
    sessionPacket.requestedScheduler = packet.requestedScheduler;
    sessionPacket.generationMultiplier = packet.generationMultiplier;
    sessionPacket.compatibility = packet.compatibility;
    result.session = session_.Resolve(sessionPacket);
    return result;
}

std::optional<D3D12CarrierWork> D3D12CarrierSession::BeginWork(
    const D3D12CarrierFrameResult& frame, std::uint64_t viewKey) noexcept {
    if (!frame) return std::nullopt;
    const auto ticket = session_.BeginWork(frame.session, viewKey);
    if (!ticket) return std::nullopt;
    return D3D12CarrierWork{*ticket, ticket->id};
}

bool D3D12CarrierSession::SubmitWork(const D3D12CarrierWork& work) noexcept {
    return work && session_.SubmitWork(work.ticket);
}

bool D3D12CarrierSession::AbandonWork(const D3D12CarrierWork& work) noexcept {
    return work && session_.AbandonWork(work.ticket);
}

bool D3D12CarrierSession::MapTimedWork(const D3D12CarrierWork& work) noexcept {
    return work && session_.MapTimedWork(work.ticket);
}

bool D3D12CarrierSession::RetireTimedInterval(double gpuMs) {
    return session_.RetireTimedInterval(gpuMs);
}

} // namespace nrfusion
