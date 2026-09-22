#include "nrfusion/D3D12NrExecutor.hpp"

#include <algorithm>

namespace nrfusion {
namespace {

bool TransitionExternal(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
                        D3D12_RESOURCE_STATES& state,
                        D3D12_RESOURCE_STATES next) noexcept {
    if (cmd == nullptr || resource == nullptr) return false;
    if (state == next) return true;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = state;
    barrier.Transition.StateAfter = next;
    cmd->ResourceBarrier(1, &barrier);
    state = next;
    return true;
}

D3D12NrCodecConstants ResolveConstants(
    const D3D12NrFrameRequest& request, std::uint32_t width,
    std::uint32_t height) noexcept {
    D3D12NrCodecConstants c{};
    c.mode = static_cast<std::uint32_t>(D3D12NrCodecMode::Resolve);
    c.whitePoint = std::max(request.composition.whitePoint, 0.01f);
    c.width = width;
    c.height = height;
    c.transferStrength = request.composition.transferStrength;
    c.colourStrength = request.composition.colourStrength;
    c.debugView = request.composition.debugView;
    c.compareMode = request.composition.compareMode;
    c.compareSplit = request.composition.compareSplit;
    c.compareZoom = request.composition.compareZoom;
    c.compareSwap = request.composition.compareSwap;
    c.debugScale = request.composition.debugScale;
    c.maxRatio = std::max(request.composition.maxRatio, 1.0f);
    c.passthrough = request.composition.colourIsLinearHdr ? 0u : 1u;
    c.transfer = request.composition.transfer;
    c.reversibleMode = request.composition.reversibleMode;
    c.applyModel = request.composition.applyModel ? 1u : 0u;
    c.useGameExposure = request.composition.useGameExposure ? 1u : 0u;
    c.exposurePreMul = request.composition.exposurePreMul;
    c.skinProtection = request.composition.skinProtection;
    c.showSkinMask = request.composition.showSkinMask;
    c.skinDetail = request.composition.skinDetail;
    c.skinColour = request.composition.skinColour;
    c.environmentDetail = request.composition.environmentDetail;
    c.environmentColour = request.composition.environmentColour;
    return c;
}

void CopyCompactToSubrect(
    ID3D12GraphicsCommandList* cmd, ID3D12Resource* dst,
    ID3D12Resource* src, const D3D12NrSubrect& rect) noexcept {
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = src;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION target = source;
    target.pResource = dst;
    D3D12_BOX box{0, 0, 0, rect.width, rect.height, 1};
    cmd->CopyTextureRegion(&target, rect.x, rect.y, 0, &source, &box);
}

} // namespace

bool D3D12NrExecutor::RunFrameModel(
    ID3D12GraphicsCommandList* cmd, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request, FrameContext& context,
    std::uint32_t effectivePasses) noexcept {
    ID3D12Resource* const primaryOutput =
        scratch_.Get(D3D12NrScratchKind::Output);
    ID3D12Resource* const passScratch =
        scratch_.Get(D3D12NrScratchKind::PassScratch);
    ID3D12Resource* passInput = context.modelInput;
    ID3D12Resource* passOutput = primaryOutput;
    ID3D12Resource* finalAnswer = nullptr;

    for (std::uint32_t pass = 0; pass < effectivePasses; ++pass) {
        void* feature = pass == 0 ? feature_ : passFeatures_[pass];
        const bool reset = request.reset || (pass > 0 && passNeedsReset_[pass]);
        if (!EvaluateFeature(
                feature, cmd, passInput, context.depthIn, context.motionIn, passOutput,
                context.plan.work.width, context.plan.work.height,
                context.plan.depth.width, context.plan.depth.height,
                context.plan.motion.width, context.plan.motion.height,
                context.plan.depth.x, context.plan.depth.y,
                context.plan.motion.x, context.plan.motion.y,
                request.depthInverted, reset, request.tuning[pass],
                request.motionScaleX * context.plan.motionToWorkX,
                request.motionScaleY * context.plan.motionToWorkY))
            return false;

        if (pass > 0) passNeedsReset_[pass] = false;
        const D3D12NrScratchKind outputKind =
            passOutput == primaryOutput
                ? D3D12NrScratchKind::Output
                : D3D12NrScratchKind::PassScratch;
        if (!scratch_.Transition(
                cmd, outputKind, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
        finalAnswer = passOutput;

        if (pass + 1 < effectivePasses) {
            passInput = finalAnswer;
            passOutput = passOutput == primaryOutput ? passScratch : primaryOutput;
            const D3D12NrScratchKind nextKind =
                passOutput == primaryOutput
                    ? D3D12NrScratchKind::Output
                    : D3D12NrScratchKind::PassScratch;
            if (scratch_.State(nextKind) == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE &&
                !scratch_.Transition(
                    cmd, nextKind, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                return false;
        }
    }

    if (finalAnswer == nullptr) return false;
    ID3D12Resource* resolveOriginal = context.acrossRr
        ? scratch_.Get(D3D12NrScratchKind::HdrCopy)
        : (context.targetSupportsUav
               ? scratch_.Get(D3D12NrScratchKind::HdrCopy)
               : context.activeTarget);
    ID3D12Resource* resolveTarget = context.acrossRr
        ? scratch_.Get(D3D12NrScratchKind::ResidualEdited)
        : (context.targetSupportsUav
               ? context.activeTarget
               : scratch_.Get(D3D12NrScratchKind::HdrCopy));

    if (context.acrossRr) {
        const D3D12_RESOURCE_STATES state =
            scratch_.State(D3D12NrScratchKind::ResidualEdited);
        if (state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
            !scratch_.Transition(
                cmd, D3D12NrScratchKind::ResidualEdited, state,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return false;
    } else if (context.targetSupportsUav) {
        if (context.cropColor) {
            const D3D12_RESOURCE_STATES state =
                scratch_.State(D3D12NrScratchKind::ActiveColor);
            if (state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
                !scratch_.Transition(
                    cmd, D3D12NrScratchKind::ActiveColor, state,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                return false;
        } else if (!TransitionExternal(
                       cmd, context.target, context.targetState,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
            return false;
        }
    } else if (!scratch_.Transition(
                   cmd, D3D12NrScratchKind::HdrCopy,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS)) {
        return false;
    }

    D3D12NrCodecConstants resolve = ResolveConstants(
        request, context.plan.activeColor.width, context.plan.activeColor.height);
    if (context.acrossRr) resolve.transferStrength = 1.0f;
    D3D12NrCodecResources resolveResources{};
    resolveResources.source = context.modelInput;
    resolveResources.model = finalAnswer;
    resolveResources.original = resolveOriginal;
    resolveResources.motion = context.motionIn;
    resolveResources.previousEdit =
        request.composition.useGameExposure ? resources.exposure : nullptr;
    resolveResources.target = resolveTarget;
    if (!codec_.Dispatch(cmd, resolve, resolveResources)) return false;

    if (context.acrossRr) {
        if (!scratch_.Transition(
                cmd, D3D12NrScratchKind::ResidualEdited,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
        const D3D12NrScratchKind previous = residualHistoryIndex_ == 0
            ? D3D12NrScratchKind::ResidualHistory0
            : D3D12NrScratchKind::ResidualHistory1;
        const D3D12NrScratchKind current = residualHistoryIndex_ == 0
            ? D3D12NrScratchKind::ResidualHistory1
            : D3D12NrScratchKind::ResidualHistory0;
        const D3D12_RESOURCE_STATES previousState = scratch_.State(previous);
        if (previousState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE &&
            !scratch_.Transition(
                cmd, previous, previousState,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
        const D3D12_RESOURCE_STATES currentState = scratch_.State(current);
        if (currentState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
            !scratch_.Transition(
                cmd, current, currentState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return false;

        D3D12NrCodecConstants accum{};
        accum.mode = 0;
        accum.width = context.plan.activeColor.width;
        accum.height = context.plan.activeColor.height;
        accum.residualBlend =
            std::clamp(request.composition.residualBlend, 0.01f, 1.0f);
        accum.residualHistoryValid = residualHistoryPrimed_ ? 1u : 0u;
        accum.guideWidth = context.plan.motion.width;
        accum.guideHeight = context.plan.motion.height;
        accum.residualMotionBaseX = context.plan.motion.x;
        accum.residualMotionBaseY = context.plan.motion.y;
        accum.motionScaleX = request.motionScaleX /
            static_cast<float>(context.plan.activeColor.width);
        accum.motionScaleY = request.motionScaleY /
            static_cast<float>(context.plan.activeColor.height);

        D3D12NrCodecResources residual{};
        residual.source = scratch_.Get(D3D12NrScratchKind::HdrCopy);
        residual.model = scratch_.Get(D3D12NrScratchKind::ResidualEdited);
        residual.original = scratch_.Get(previous);
        residual.motion = context.motionIn;
        residual.target = scratch_.Get(current);
        if (!codec_.DispatchResidual(cmd, accum, residual) ||
            !scratch_.Transition(
                cmd, current, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
        residualHistoryIndex_ ^= 1u;
        residualHistoryPrimed_ = true;
        residualStoreValid_ = true;
        residualEpoch_ = request.submissionEpoch;
        return true;
    }

    if (!context.targetSupportsUav) {
        if (!scratch_.Transition(
                cmd, D3D12NrScratchKind::HdrCopy,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
            !TransitionExternal(
                cmd, context.activeTarget, context.targetState,
                D3D12_RESOURCE_STATE_COPY_DEST))
            return false;
        cmd->CopyResource(
            context.activeTarget, scratch_.Get(D3D12NrScratchKind::HdrCopy));
        if (!TransitionExternal(
                cmd, context.activeTarget, context.targetState,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
            !scratch_.Transition(
                cmd, D3D12NrScratchKind::HdrCopy,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
    }

    if (context.cropColor) {
        if (!scratch_.Transition(
                cmd, D3D12NrScratchKind::ActiveColor,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
            !TransitionExternal(
                cmd, context.target, context.targetState,
                D3D12_RESOURCE_STATE_COPY_DEST))
            return false;
        CopyCompactToSubrect(
            cmd, context.target,
            scratch_.Get(D3D12NrScratchKind::ActiveColor),
            context.plan.activeColor);
        if (!TransitionExternal(
                cmd, context.target, context.targetState, context.targetArrival) ||
            !scratch_.Transition(
                cmd, D3D12NrScratchKind::ActiveColor,
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return false;
    }
    return true;
}

} // namespace nrfusion
