#include "nrfusion/PipelinePolicy.hpp"

namespace nrfusion {
namespace {

MotionSource ChooseMotion(const FrameContext& frame, const RuntimeCapabilities& caps) noexcept {
    if (caps.nativeMotion && frame.HasNativeMotion() && frame.MotionReliable(MotionSource::Native))
        return MotionSource::Native;
    if (caps.dlssContractMotion && frame.HasDlssContractMotion() &&
        frame.MotionReliable(MotionSource::DlssContract))
        return MotionSource::DlssContract;
    if (caps.nvof)
        return MotionSource::NvidiaOpticalFlow;
    if (caps.shaderMotion && frame.HasDepth() && frame.depthReliable)
        return MotionSource::ShaderEstimated;
    return MotionSource::Zero;
}

NrPlacement ChoosePlacement(const GameContext& game, FrameProvider provider,
                            const RuntimeCapabilities& caps) noexcept {
    if (provider == FrameProvider::Unsupported)
        return caps.postSr ? NrPlacement::PostSr : NrPlacement::Auto;

    if (game.rayReconstruction && game.api != GraphicsApi::Vulkan && caps.acrossRr)
        return NrPlacement::AcrossRr;
    if (caps.preSr)
        return NrPlacement::PreSr;
    if (caps.deferredResidual)
        return NrPlacement::DeferredResidual;
    if (caps.postSr)
        return NrPlacement::PostSr;
    return NrPlacement::Auto;
}

} // namespace

PipelineDecision PipelinePolicy::Choose(const GameContext& game, const FrameContext& frame,
                                        const RuntimeCapabilities& caps,
                                        std::optional<FrameProvider> preferredProvider) const {
    const FrameProvider provider = provider_.Choose(game, caps, preferredProvider);
    const ProcessTransport transport = transport_.Choose(game);
    const NrPlacement placement = ChoosePlacement(game, provider, caps);
    const bool supported = provider != FrameProvider::Unsupported &&
                           transport_.IsSupported(game, caps) &&
                           placement != NrPlacement::Auto;
    return {provider, transport, game.api, ChooseMotion(frame, caps), placement, supported};
}

} // namespace nrfusion
