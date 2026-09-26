#include "nrfusion/FusionRuntime.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

FrameContext ReadyFrame(FrameId id) {
    FrameContext frame{};
    frame.frameId = id;
    frame.configurationGeneration = 1;
    frame.api = GraphicsApi::D3D12;
    frame.renderResolution = {1920, 1080};
    frame.outputResolution = {1920, 1080};
    frame.color = {
        1, {1920, 1080}, ResourceFormat::Rgba16Float};
    frame.motionVectors = {
        2, {1920, 1080}, ResourceFormat::Rg16Float};
    frame.motionVectorSource = MotionSource::Native;
    frame.motionVectorsReliable = true;
    return frame;
}

RuntimeCapabilities Caps() {
    RuntimeCapabilities caps{};
    caps.nativeProvider = true;
    caps.preSr = true;
    caps.nativeMotion = true;
    caps.shaderMotion = true;
    caps.nvof = true;
    caps.fp8 = true;
    return caps;
}

} // namespace

int main() {
    FusionRuntime runtime;
    GameContext game{};
    game.api = GraphicsApi::D3D12;
    game.nativeDlss = true;
    TelemetrySample telemetry{};

    auto frame = ReadyFrame(1);
    auto caps = Caps();
    auto first = runtime.ResolveAuto(
        game, frame, telemetry, caps);
    assert(first.supported);
    assert(first.pipeline.motion == MotionSource::Native);
    const auto generation =
        runtime.AutoConfigurationGeneration();

    frame.frameId = 2;
    frame.cameraCut = true;
    const auto cut = runtime.ResolveAuto(
        game, frame, telemetry, caps);
    assert(cut.pipeline.motion == MotionSource::Zero);
    assert(runtime.AutoConfigurationGeneration() == generation);

    frame.frameId = 3;
    frame.cameraCut = false;
    const auto after = runtime.ResolveAuto(
        game, frame, telemetry, caps);
    assert(after.pipeline.motion == MotionSource::Native);
    assert(runtime.AutoConfigurationGeneration() == generation);

    frame.frameId = 4;
    frame.resetHistory = true;
    const auto reset = runtime.ResolveAuto(
        game, frame, telemetry, caps);
    assert(reset.pipeline.motion == MotionSource::Zero);
    assert(runtime.AutoConfigurationGeneration() == generation);

    frame.frameId = 5;
    frame.resetHistory = false;
    caps.nativeMotion = false;
    caps.nvofGuideReady = false;
    const auto noReadyNvof = runtime.ResolveAuto(
        game, frame, telemetry, caps);
    assert(noReadyNvof.pipeline.motion == MotionSource::Zero);

    ViewDescriptor view{};
    view.featureKey = 9;
    view.width = 1920;
    view.height = 1080;
    const auto lease = runtime.AcquireHistory(
        view, frame, noReadyNvof.pipeline.motion);
    assert(lease.resetRequired);

    MotionGuideSample sample{};
    sample.present = true;
    sample.validPixelRatio = 1.0;
    sample.temporalAgreement = 1.0;
    sample.depthAgreement = 1.0;
    sample.magnitudeSanity = 1.0;
    runtime.UpdateMotionGuide(sample);
    assert(runtime.UpdateMotionGuide(sample).reliable);
    sample.resetHistory = true;
    assert(!runtime.UpdateMotionGuide(sample).reliable);
    return 0;
}
