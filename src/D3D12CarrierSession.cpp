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
    result.acquire = BuildD3D12FrameContract(packet.acquire);
    if (!result.acquire) return result;

    NrSessionFramePacket sessionPacket{};
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

} // namespace nrfusion
