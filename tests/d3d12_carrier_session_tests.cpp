#include "nrfusion/D3D12CarrierSession.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

D3D12AcquireSnapshot Snapshot(
    std::uint64_t generation, FrameId frameId) {
    D3D12AcquireSnapshot snapshot{};
    snapshot.identity.frameId = frameId;
    snapshot.identity.configurationGeneration = generation;
    snapshot.renderResolution = {1920, 1080};
    snapshot.outputResolution = {1920, 1080};

    ResourceRef color{};
    color.opaqueId = 1;
    color.resolution = snapshot.renderResolution;
    color.format = ResourceFormat::Rgba16Float;
    color.provenance = ResourceProvenance::GameNative;
    color.reliability = ResourceReliability::Reliable;
    color.ownership = ResourceOwnership::Borrowed;
    color.lifetime = ResourceLifetime::Frame;
    color.sourceFrameId = frameId;
    snapshot.color = {color, true};
    return snapshot;
}

D3D12CarrierFramePacket Packet(
    std::uint64_t generation, FrameId frameId) {
    D3D12CarrierFramePacket packet{};
    packet.game.api = GraphicsApi::D3D12;
    packet.acquire = Snapshot(generation, frameId);
    packet.capabilities.syntheticD3D12 = true;
    packet.capabilities.postSr = true;
    packet.capabilities.fp8 = true;
    packet.telemetry.dtSeconds = 1.0 / 60.0;
    packet.telemetry.nrGpuMs = 2.0;
    packet.telemetry.frameGpuMs = 8.0;
    packet.telemetry.sourceFps = 60.0;
    packet.telemetry.processedFps = 60.0;
    return packet;
}

} // namespace

int main() {
    RuntimeConfig config{};
    config.generation = 7;
    config.enabled = true;
    config.targetFps = 60.0f;

    PerformanceConfig performance{};
    performance.targetFps = 60.0;

    D3D12CarrierSession carrier;
    assert(carrier.Configure(config, performance));

    const auto ready = carrier.Resolve(Packet(config.generation, 1));
    assert(ready);
    assert(ready.acquire.frame.api == GraphicsApi::D3D12);
    assert(ready.session.disposition == NrSessionDisposition::Ready);
    assert(ready.session.configurationGeneration == config.generation);
    assert(ready.session.decision.pipeline.api == GraphicsApi::D3D12);
    assert(carrier.State().resolvedFrames == 1);

    auto unproven = Packet(config.generation, 2);
    unproven.acquire.color.acquired = false;
    const auto rejectedAcquire = carrier.Resolve(unproven);
    assert(!rejectedAcquire);
    assert(rejectedAcquire.acquire.failure == D3D12AcquireFailure::UnprovenResource);
    assert(carrier.State().resolvedFrames == 1);
    assert(carrier.State().rejectedFrames == 0);

    const auto stale = carrier.Resolve(Packet(config.generation - 1, 3));
    assert(!stale);
    assert(stale.acquire);
    assert(stale.session.disposition == NrSessionDisposition::StaleConfiguration);
    assert(carrier.State().rejectedFrames == 1);

    auto wrongApi = Packet(config.generation, 4);
    wrongApi.game.api = GraphicsApi::Vulkan;
    const auto mismatched = carrier.Resolve(wrongApi);
    assert(!mismatched);
    assert(mismatched.acquire);
    assert(mismatched.session.disposition == NrSessionDisposition::InvalidFrame);

    RuntimeConfig disabled = config;
    ++disabled.generation;
    disabled.enabled = false;
    assert(carrier.Configure(disabled, performance));
    const auto passThrough = carrier.Resolve(Packet(disabled.generation, 5));
    assert(!passThrough);
    assert(passThrough.acquire);
    assert(passThrough.session.disposition == NrSessionDisposition::Disabled);

    carrier.Reset();
    const auto afterReset = carrier.Resolve(Packet(config.generation, 6));
    assert(!afterReset);
    assert(afterReset.acquire);
    assert(afterReset.session.disposition == NrSessionDisposition::NotConfigured);
    return 0;
}
