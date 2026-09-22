#include "nrfusion/D3D12CarrierNativeFacts.hpp"
#include "nrfusion/D3D12CarrierSession.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

D3D12CarrierFramePacket Packet(std::uint64_t generation, FrameId frameId) {
    D3D12NativeAcquireInput input{};
    input.identity.frameId = frameId;
    input.identity.configurationGeneration = generation;
    input.color.texture = {
        11, {1920, 1080}, ResourceFormat::Rgba16Float, 1, 1, 1, true
    };
    input.color.provenance = ResourceProvenance::GameNative;
    input.color.reliability = ResourceReliability::Reliable;
    input.output = {
        12, {1920, 1080}, ResourceFormat::Unknown, 1, 1, 1, true
    };
    const auto native = BuildD3D12NativeAcquireSnapshot(input);
    assert(native);

    D3D12CarrierFramePacket packet{};
    packet.game.api = GraphicsApi::D3D12;
    packet.acquire = native.snapshot;
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
    config.generation = 17;
    config.enabled = true;
    config.targetFps = 60.0f;

    PerformanceConfig performance{};
    performance.targetFps = 60.0;

    D3D12CarrierSession carrier;
    assert(carrier.Configure(config, performance));

    const auto frame = carrier.Resolve(Packet(config.generation, 101));
    assert(frame);

    const auto first = carrier.BeginWork(frame, 77);
    assert(first);
    assert(*first);
    assert(first->submissionEpoch == first->ticket.id);
    assert(first->ticket.sourceFrame == frame.session.frameId);
    assert(first->ticket.viewKey == 77);
    assert(first->ticket.configurationGeneration ==
           frame.session.runtimeGeneration);

    assert(carrier.SubmitWork(*first));
    assert(!carrier.SubmitWork(*first));
    assert(carrier.MapTimedWork(*first));
    assert(!carrier.MapTimedWork(*first));
    assert(carrier.RetireTimedInterval(2.0));
    assert(!carrier.AbandonWork(*first));

    const auto second = carrier.BeginWork(frame, 88);
    assert(second);
    assert(second->submissionEpoch > first->submissionEpoch);
    assert(carrier.AbandonWork(*second));

    auto invalidPacket = Packet(config.generation, 102);
    invalidPacket.acquire.color.acquired = false;
    const auto invalidFrame = carrier.Resolve(invalidPacket);
    assert(!invalidFrame);
    assert(!carrier.BeginWork(invalidFrame));

    const auto pending = carrier.BeginWork(frame, 99);
    assert(pending);

    RuntimeConfig next = config;
    ++next.generation;
    assert(carrier.Configure(next, performance));
    assert(!carrier.SubmitWork(*pending));
    assert(!carrier.MapTimedWork(*pending));
    assert(!carrier.AbandonWork(*pending));

    const auto nextFrame = carrier.Resolve(Packet(next.generation, 103));
    assert(nextFrame);
    const auto nextWork = carrier.BeginWork(nextFrame);
    assert(nextWork);
    assert(nextWork->submissionEpoch > pending->submissionEpoch);
    assert(carrier.AbandonWork(*nextWork));
    return 0;
}
