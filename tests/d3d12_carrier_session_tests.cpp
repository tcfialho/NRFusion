#include "nrfusion/D3D12CarrierSession.hpp"
#include "nrfusion/D3D12CarrierNativeFacts.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

D3D12AcquireSnapshot Snapshot(
    std::uint64_t generation, FrameId frameId) {
    D3D12NativeAcquireInput input{};
    input.identity.frameId = frameId;
    input.identity.configurationGeneration = generation;
    input.color.texture = {
        1, {1920, 1080}, ResourceFormat::Rgba16Float, 1, 1, 1, true
    };
    input.color.provenance = ResourceProvenance::GameNative;
    input.color.reliability = ResourceReliability::Reliable;
    input.output = {
        2, {1920, 1080}, ResourceFormat::Unknown, 1, 1, 1, true
    };
    const auto acquired = BuildD3D12NativeAcquireSnapshot(input);
    assert(acquired);
    return acquired.snapshot;
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
    assert(!stale.acquire.attempted);
    assert(stale.session.disposition == NrSessionDisposition::StaleConfiguration);
    assert(carrier.State().rejectedFrames == 1);

    auto wrongApi = Packet(config.generation, 4);
    wrongApi.game.api = GraphicsApi::Vulkan;
    const auto mismatched = carrier.Resolve(wrongApi);
    assert(!mismatched);
    assert(!mismatched.acquire.attempted);
    assert(mismatched.session.disposition == NrSessionDisposition::InvalidFrame);

    RuntimeConfig disabled = config;
    ++disabled.generation;
    disabled.enabled = false;
    assert(carrier.Configure(disabled, performance));
    auto disabledPacket = Packet(disabled.generation, 5);
    disabledPacket.acquire.color = {};
    const auto passThrough = carrier.Resolve(disabledPacket);
    assert(!passThrough);
    assert(!passThrough.acquire.attempted);
    assert(passThrough.session.disposition == NrSessionDisposition::Disabled);

    carrier.Reset();
    auto resetPacket = Packet(config.generation, 6);
    resetPacket.acquire.color = {};
    const auto afterReset = carrier.Resolve(resetPacket);
    assert(!afterReset);
    assert(!afterReset.acquire.attempted);
    assert(afterReset.session.disposition == NrSessionDisposition::NotConfigured);
    return 0;
}
