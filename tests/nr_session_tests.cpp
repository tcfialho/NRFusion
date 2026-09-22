#include "nrfusion/NrSession.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

using namespace nrfusion;

namespace {

NrSessionFramePacket Packet(std::uint64_t generation, FrameId frameId) {
    NrSessionFramePacket packet{};
    packet.game.api = GraphicsApi::D3D12;

    packet.frame.frameId = frameId;
    packet.frame.configurationGeneration = generation;
    packet.frame.api = GraphicsApi::D3D12;
    packet.frame.renderResolution = {1920, 1080};
    packet.frame.outputResolution = {1920, 1080};
    packet.frame.color.opaqueId = 1;
    packet.frame.color.resolution = packet.frame.renderResolution;
    packet.frame.color.format = ResourceFormat::Rgba16Float;

    packet.capabilities.syntheticD3D12 = true;
    packet.capabilities.postSr = true;
    packet.capabilities.fp8 = true;
    packet.telemetry.dtSeconds = 1.0 / 60.0;
    return packet;
}

void AssertEquivalent(const AutoDecision& a, const AutoDecision& b) {
    assert(a.supported == b.supported);
    assert(a.pipeline.provider == b.pipeline.provider);
    assert(a.pipeline.transport == b.pipeline.transport);
    assert(a.pipeline.api == b.pipeline.api);
    assert(a.pipeline.motion == b.pipeline.motion);
    assert(a.pipeline.placement == b.pipeline.placement);
    assert(a.scheduler == b.scheduler);
    assert(a.precision == b.precision);
    assert(std::fabs(a.workingScale - b.workingScale) < 1e-6f);
    assert(a.presentation == b.presentation);
    assert(a.generationMultiplier == b.generationMultiplier);
}

} // namespace

int main() {
    PerformanceConfig performance{};
    performance.targetFps = 60.0;

    RuntimeConfig config{};
    config.generation = 7;
    config.enabled = true;
    config.targetFps = 60.0f;

    NrSession session;
    assert(session.Configure(config, performance));

    FusionRuntime baseline(performance);
    baseline.BeginConfigurationEpoch(baseline.PerformanceCfg().maxScale);

    auto packet = Packet(config.generation, 1);
    const auto expected = baseline.ResolveAuto(
        packet.game, packet.frame, packet.telemetry, packet.capabilities);
    const auto actual = session.Resolve(packet);
    assert(actual);
    AssertEquivalent(actual.decision, expected);
    assert(actual.configurationGeneration == config.generation);
    assert(actual.runtimeGeneration == baseline.AutoConfigurationGeneration());
    assert(actual.runtimeGeneration == session.State().runtimeGeneration);

    const auto work = session.BeginWork(actual, 9);
    assert(work);
    assert(session.SubmitWork(*work));
    assert(session.MapTimedWork(*work));
    assert(!session.MapTimedWork(*work));

    auto resized = Packet(config.generation, 2);
    resized.frame.renderResolution = {1280, 720};
    resized.frame.outputResolution = {1280, 720};
    resized.frame.color.resolution = resized.frame.renderResolution;
    const auto resizedResult = session.Resolve(resized);
    assert(resizedResult);
    assert(resizedResult.runtimeGeneration != work->configurationGeneration);
    assert(!session.RetireTimedInterval(2.0));

    auto stale = Packet(config.generation - 1, 3);
    const auto staleResult = session.Resolve(stale);
    assert(staleResult.disposition == NrSessionDisposition::StaleConfiguration);
    assert(session.State().lastResolvedFrame == 2);

    RuntimeConfig older = config;
    --older.generation;
    assert(!session.Configure(older, performance));
    assert(session.Config() == config);

    PerformanceConfig changedPerformance = performance;
    changedPerformance.targetFps = 120.0;
    assert(!session.Configure(config, changedPerformance));
    assert(session.Config() == config);

    PerformanceConfig invalidPerformance = performance;
    invalidPerformance.scaleSteps = {
        std::numeric_limits<float>::quiet_NaN()
    };
    RuntimeConfig invalidGeneration = config;
    ++invalidGeneration.generation;
    assert(!session.Configure(invalidGeneration, invalidPerformance));
    assert(session.Config() == config);
    const auto stillCurrent = session.Resolve(Packet(config.generation, 9));
    assert(stillCurrent);

    FusionRuntime sequenceBaseline(performance);
    sequenceBaseline.BeginConfigurationEpoch(sequenceBaseline.PerformanceCfg().maxScale);
    NrSession sequenceSession;
    assert(sequenceSession.Configure(config, performance));
    for (FrameId frameId = 10; frameId < 190; ++frameId) {
        auto sequencePacket = Packet(config.generation, frameId);
        sequencePacket.telemetry.nrGpuMs = frameId < 100 ? 1.5 : 5.0;
        sequencePacket.telemetry.frameGpuMs = frameId < 100 ? 8.0 : 12.0;
        sequencePacket.telemetry.sourceFps = 60.0;
        sequencePacket.telemetry.processedFps = 60.0;
        const AutoDecision expectedSequence = sequenceBaseline.ResolveAuto(
            sequencePacket.game, sequencePacket.frame, sequencePacket.telemetry,
            sequencePacket.capabilities);
        const NrSessionFrameResult actualSequence = sequenceSession.Resolve(sequencePacket);
        assert(actualSequence);
        AssertEquivalent(actualSequence.decision, expectedSequence);
        assert(actualSequence.runtimeGeneration ==
               sequenceBaseline.AutoConfigurationGeneration());
    }

    RuntimeConfig precisionConfig = config;
    precisionConfig.generation = 20;
    NrSession precisionSession;
    assert(precisionSession.Configure(precisionConfig, performance));
    auto precisionPacket = Packet(precisionConfig.generation, 300);
    precisionPacket.capabilities.hybridNvfp4 = true;
    bool sawHybrid = false;
    std::uint64_t fp8Generation = 0;
    std::optional<WorkTicket> pendingFp8;
    for (std::size_t i = 0; i < 110; ++i) {
        const auto precisionFrame = precisionSession.Resolve(precisionPacket);
        assert(precisionFrame);
        if (!sawHybrid &&
            precisionFrame.decision.precision == NrPrecision::HybridNvfp4) {
            sawHybrid = true;
            assert(fp8Generation != 0);
            assert(precisionFrame.runtimeGeneration != fp8Generation);
            assert(pendingFp8);
            assert(!precisionSession.MapTimedWork(*pendingFp8));
            assert(precisionSession.AbandonWork(*pendingFp8));
            pendingFp8.reset();
        }
        const auto precisionWork = precisionSession.BeginWork(precisionFrame);
        assert(precisionWork && precisionSession.SubmitWork(*precisionWork));
        if (!sawHybrid && !pendingFp8) {
            fp8Generation = precisionFrame.runtimeGeneration;
            pendingFp8 = *precisionWork;
        } else {
            assert(precisionSession.MapTimedWork(*precisionWork));
        }
        const double gpuMs =
            precisionFrame.decision.precision == NrPrecision::HybridNvfp4
                ? 1.5 : 2.0;
        if (!pendingFp8 || precisionWork->id != pendingFp8->id)
            assert(precisionSession.RetireTimedInterval(gpuMs));
        ++precisionPacket.frame.frameId;
    }
    assert(sawHybrid);
    assert(!pendingFp8);
    const auto qualifiedPrecision = precisionSession.Resolve(precisionPacket);
    assert(qualifiedPrecision);
    assert(qualifiedPrecision.decision.precision == NrPrecision::HybridNvfp4);

    {
        NrSession ringSession;
        assert(ringSession.Configure(config, performance));
        const auto ringFrame = ringSession.Resolve(Packet(config.generation, 500));
        assert(ringFrame);
        std::array<WorkTicket, NrSessionTimingQueue::kCapacity + 1> ringTickets{};
        for (std::size_t i = 0; i < ringTickets.size(); ++i) {
            const auto ticket = ringSession.BeginWork(ringFrame, i + 1);
            assert(ticket);
            ringTickets[i] = *ticket;
            assert(ringSession.SubmitWork(ringTickets[i]));
            assert(ringSession.MapTimedWork(ringTickets[i]));
        }
        assert(!ringSession.AbandonWork(ringTickets.front()));
        assert(ringSession.RetireTimedInterval(2.0));
        assert(!ringSession.AbandonWork(ringTickets[1]));
    }

    RuntimeConfig disabled = config;
    ++disabled.generation;
    disabled.enabled = false;
    assert(session.Configure(disabled, performance));
    assert(!session.RetireTimedInterval(2.0));
    assert(!session.SubmitWork(*work));
    auto disabledPacket = Packet(disabled.generation, 3);
    assert(session.Resolve(disabledPacket).disposition == NrSessionDisposition::Disabled);

    RuntimeConfig enabledAgain = disabled;
    ++enabledAgain.generation;
    enabledAgain.enabled = true;
    assert(session.Configure(enabledAgain, performance));
    auto nextPacket = Packet(enabledAgain.generation, 4);
    const auto next = session.Resolve(nextPacket);
    assert(next);
    const auto nextWork = session.BeginWork(next);
    assert(nextWork && session.SubmitWork(*nextWork));
    session.MapInvalidTimedAttempt();
    assert(!session.RetireTimedInterval(2.0));
    assert(session.AbandonWork(*nextWork));

    session.Reset();
    assert(session.Resolve(packet).disposition == NrSessionDisposition::NotConfigured);
    assert(session.Configure(config, performance));
    assert(session.Resolve(Packet(config.generation, 200)));
    return 0;
}
