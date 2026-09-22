#include "nrfusion/D3D12NrExecutor.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {
namespace {

bool Transition(ID3D12GraphicsCommandList* cmd, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES next) noexcept {
    if (resource == nullptr || cmd == nullptr) return false;
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

DXGI_FORMAT TypedGuideFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return format;
    }
}

void CopyRect(ID3D12GraphicsCommandList* cmd, ID3D12Resource* dst,
              ID3D12Resource* src, const D3D12NrSubrect& rect) noexcept {
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = src;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION target = source;
    target.pResource = dst;
    D3D12_BOX box{rect.x, rect.y, 0, rect.x + rect.width, rect.y + rect.height, 1};
    cmd->CopyTextureRegion(&target, 0, 0, 0, &source, &box);
}

D3D12NrCodecConstants ComposeConstants(
    const D3D12NrFrameRequest& request, D3D12NrCodecMode mode,
    std::uint32_t width, std::uint32_t height) noexcept {
    D3D12NrCodecConstants c{};
    c.mode = static_cast<std::uint32_t>(mode);
    c.whitePoint = std::max(request.composition.whitePoint, 0.01f);
    c.width = width;
    c.height = height;
    c.transferStrength = request.composition.transferStrength;
    c.colourStrength = request.composition.colourStrength;
    c.debugView = request.composition.debugView;
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

} // namespace

D3D12NrFrameResult D3D12NrExecutor::ExecuteFrame(
    ID3D12GraphicsCommandList* cmd, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request) {
    if (cmd == nullptr || resources.color == nullptr || resources.depth == nullptr ||
        resources.motion == nullptr || resources.output == nullptr)
        return D3D12NrFrameResult::Failed;

    const bool before = request.plan.beforeUpscale;
    const bool acrossRr = request.composition.residualAcrossRr &&
                          request.composition.runBeforeUpscale &&
                          request.composition.rayReconstruction;

    if (acrossRr && !before) {
        if (!residualStoreValid_ || request.submissionEpoch != residualEpoch_)
            return D3D12NrFrameResult::SkippedPlacement;

        ID3D12Device* device = nullptr;
        if (FAILED(resources.output->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
            return D3D12NrFrameResult::Failed;
        const D3D12_RESOURCE_DESC outDesc = resources.output->GetDesc();
        const bool ready = codec_.Init(device) &&
            scratch_.EnsureOptional(device, D3D12NrScratchKind::ResidualComposed,
                                    outDesc.Format, static_cast<std::uint32_t>(outDesc.Width),
                                    outDesc.Height, retirement_);
        device->Release();
        if (!ready) return D3D12NrFrameResult::Failed;

        D3D12_RESOURCE_STATES outputState = request.outputState;
        if (!scratch_.Transition(cmd, D3D12NrScratchKind::ResidualComposed,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST) ||
            !Transition(cmd, resources.output, outputState, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return D3D12NrFrameResult::Failed;
        cmd->CopyResource(scratch_.Get(D3D12NrScratchKind::ResidualComposed), resources.output);
        if (!scratch_.Transition(cmd, D3D12NrScratchKind::ResidualComposed,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
            !Transition(cmd, resources.output, outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            return D3D12NrFrameResult::Failed;

        D3D12NrCodecConstants c{};
        c.mode = 1;
        c.width = static_cast<std::uint32_t>(outDesc.Width);
        c.height = outDesc.Height;
        c.transferStrength = request.composition.transferStrength;
        D3D12NrCodecResources r{};
        r.source = scratch_.Get(D3D12NrScratchKind::ResidualComposed);
        r.model = scratch_.Get(residualHistoryIndex_ == 0
            ? D3D12NrScratchKind::ResidualHistory0 : D3D12NrScratchKind::ResidualHistory1);
        r.target = resources.output;
        if (!codec_.DispatchResidual(cmd, c, r)) return D3D12NrFrameResult::Failed;
        Transition(cmd, resources.output, outputState, request.outputState);
        scratch_.Transition(cmd, D3D12NrScratchKind::ResidualComposed,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        residualStoreValid_ = false;
        return D3D12NrFrameResult::Applied;
    }

    if (request.composition.runBeforeUpscale != before)
        return D3D12NrFrameResult::SkippedPlacement;

    D3D12NrFramePlan plan{};
    if (!BuildD3D12NrFramePlan(request.plan, plan)) return D3D12NrFrameResult::Failed;

    ID3D12Resource* target = before ? resources.color : resources.output;
    D3D12_RESOURCE_STATES targetState = before ? request.colorState : request.outputState;
    const D3D12_RESOURCE_STATES targetArrival = targetState;
    ID3D12Device* device = nullptr;
    if (FAILED(target->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
        return D3D12NrFrameResult::Failed;
    const D3D12_RESOURCE_DESC targetDesc = target->GetDesc();
    D3D12NrScratchDesc scratchDesc{
        targetDesc.Format, plan.activeColor.width, plan.activeColor.height,
        plan.work.width, plan.work.height};

    bool ok = codec_.Init(device) && scratch_.Ensure(device, scratchDesc, retirement_);
    if (plan.requestedPasses > 1)
        ok = ok && scratch_.EnsureOptional(device, D3D12NrScratchKind::PassScratch,
                                            targetDesc.Format, plan.work.width, plan.work.height, retirement_);
    if (plan.reduced)
        ok = ok && scratch_.EnsureOptional(device, D3D12NrScratchKind::ColorSmall,
                                            targetDesc.Format, plan.work.width, plan.work.height, retirement_);
    if (plan.cropColor)
        ok = ok && scratch_.EnsureOptional(device, D3D12NrScratchKind::ActiveColor,
                                            targetDesc.Format, plan.activeColor.width,
                                            plan.activeColor.height, retirement_);
    if (acrossRr) {
        const D3D12_RESOURCE_DESC outputDesc = resources.output->GetDesc();
        ok = ok &&
            scratch_.EnsureOptional(device, D3D12NrScratchKind::ResidualEdited,
                                    targetDesc.Format, plan.activeColor.width,
                                    plan.activeColor.height, retirement_) &&
            scratch_.EnsureOptional(device, D3D12NrScratchKind::ResidualHistory0,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT, plan.activeColor.width,
                                    plan.activeColor.height, retirement_) &&
            scratch_.EnsureOptional(device, D3D12NrScratchKind::ResidualHistory1,
                                    DXGI_FORMAT_R16G16B16A16_FLOAT, plan.activeColor.width,
                                    plan.activeColor.height, retirement_) &&
            scratch_.EnsureOptional(device, D3D12NrScratchKind::ResidualComposed,
                                    outputDesc.Format, static_cast<std::uint32_t>(outputDesc.Width),
                                    outputDesc.Height, retirement_);
    }
    device->Release();
    if (!ok) return D3D12NrFrameResult::Failed;

    ID3D12Resource* activeTarget = target;
    D3D12_RESOURCE_STATES activeState = targetState;
    if (plan.cropColor) {
        if (!Transition(cmd, target, targetState, D3D12_RESOURCE_STATE_COPY_SOURCE) ||
            !scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST))
            return D3D12NrFrameResult::Failed;
        CopyRect(cmd, scratch_.Get(D3D12NrScratchKind::ActiveColor), target, plan.activeColor);
        Transition(cmd, target, targetState, targetArrival);
        scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        activeTarget = scratch_.Get(D3D12NrScratchKind::ActiveColor);
        activeState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    } else if (!Transition(cmd, activeTarget, activeState,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
        return D3D12NrFrameResult::Failed;
    }

    D3D12NrCodecConstants encode = ComposeConstants(
        request, D3D12NrCodecMode::Encode, plan.activeColor.width, plan.activeColor.height);
    D3D12NrCodecResources codecResources{};
    codecResources.source = activeTarget;
    codecResources.previousEdit = request.composition.useGameExposure ? resources.exposure : nullptr;
    codecResources.target = scratch_.Get(D3D12NrScratchKind::ColorCopy);
    codecResources.keep = scratch_.Get(D3D12NrScratchKind::HdrCopy);
    if (!codec_.Dispatch(cmd, encode, codecResources)) return D3D12NrFrameResult::Failed;
    scratch_.Transition(cmd, D3D12NrScratchKind::ColorCopy,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    scratch_.Transition(cmd, D3D12NrScratchKind::HdrCopy,
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12Resource* modelInput = scratch_.Get(D3D12NrScratchKind::ColorCopy);
    if (plan.reduced) {
        D3D12NrCodecConstants scale{};
        scale.mode = static_cast<std::uint32_t>(D3D12NrCodecMode::Downsample);
        scale.width = plan.work.width;
        scale.height = plan.work.height;
        D3D12NrCodecResources r{};
        r.source = modelInput;
        r.target = scratch_.Get(D3D12NrScratchKind::ColorSmall);
        if (!codec_.Dispatch(cmd, scale, r)) return D3D12NrFrameResult::Failed;
        scratch_.Transition(cmd, D3D12NrScratchKind::ColorSmall,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        modelInput = scratch_.Get(D3D12NrScratchKind::ColorSmall);
    }

    auto prepareGuide = [&](D3D12NrGuideKind kind, ID3D12Resource* source,
                            D3D12_RESOURCE_STATES& state) -> ID3D12Resource* {
        const DXGI_FORMAT typed = TypedGuideFormat(source->GetDesc().Format);
        if (typed == source->GetDesc().Format) {
            return Transition(cmd, source, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                ? source : nullptr;
        }
        ID3D12Device* guideDevice = nullptr;
        if (FAILED(source->GetDevice(IID_PPV_ARGS(&guideDevice))) || guideDevice == nullptr) return nullptr;
        const bool ensured = guideClones_.Ensure(guideDevice, kind, source, typed, retirement_);
        guideDevice->Release();
        if (!ensured || !Transition(cmd, source, state, D3D12_RESOURCE_STATE_COPY_SOURCE)) return nullptr;
        cmd->CopyResource(guideClones_.Get(kind), source);
        Transition(cmd, source, state, kind == D3D12NrGuideKind::Depth
            ? request.depthState : request.motionState);
        return guideClones_.Transition(cmd, kind, D3D12_RESOURCE_STATE_COPY_DEST,
                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            ? guideClones_.Get(kind) : nullptr;
    };

    D3D12_RESOURCE_STATES depthState = request.depthState;
    D3D12_RESOURCE_STATES motionState = request.motionState;
    ID3D12Resource* depthIn = prepareGuide(D3D12NrGuideKind::Depth, resources.depth, depthState);
    ID3D12Resource* motionIn = prepareGuide(D3D12NrGuideKind::Motion, resources.motion, motionState);
    if (depthIn == nullptr || motionIn == nullptr) return D3D12NrFrameResult::Failed;

    if (!EnsureFeatureForEpoch(cmd, plan.work.width, plan.work.height,
                               request.submissionEpoch, request.tuning[0]))
        return D3D12NrFrameResult::Failed;
    if (!submissionGate_.ReadyFor(request.submissionEpoch))
        return D3D12NrFrameResult::PendingFeature;

    bool pending = false;
    const std::uint32_t effectivePasses = PreparePassFeatures(
        cmd, plan.work.width, plan.work.height, plan.requestedPasses,
        request.submissionEpoch, request.tuning, pending);
    if (pending) return D3D12NrFrameResult::PendingFeature;
    if (effectivePasses == 0) return D3D12NrFrameResult::Failed;

    ID3D12Resource* passInput = modelInput;
    ID3D12Resource* passOutput = scratch_.Get(D3D12NrScratchKind::Output);
    ID3D12Resource* finalAnswer = nullptr;
    bool modelOk = true;
    for (std::uint32_t pass = 0; pass < effectivePasses && modelOk; ++pass) {
        const bool reset = request.reset || (pass > 0 && passNeedsReset_[pass]);
        void* feature = pass == 0 ? feature_ : passFeatures_[pass];
        modelOk = EvaluateFeature(
            feature, cmd, passInput, depthIn, motionIn, passOutput,
            plan.work.width, plan.work.height, plan.depth.width, plan.depth.height,
            plan.motion.width, plan.motion.height, plan.depth.x, plan.depth.y,
            plan.motion.x, plan.motion.y, request.depthInverted, reset,
            request.tuning[pass],
            request.motionScaleX * plan.motionToWorkX,
            request.motionScaleY * plan.motionToWorkY);
        if (!modelOk) break;
        if (pass > 0) passNeedsReset_[pass] = false;
        D3D12NrScratchKind outKind = passOutput == scratch_.Get(D3D12NrScratchKind::Output)
            ? D3D12NrScratchKind::Output : D3D12NrScratchKind::PassScratch;
        scratch_.Transition(cmd, outKind, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        finalAnswer = passOutput;
        if (pass + 1 < effectivePasses) {
            passInput = finalAnswer;
            passOutput = passOutput == scratch_.Get(D3D12NrScratchKind::Output)
                ? scratch_.Get(D3D12NrScratchKind::PassScratch)
                : scratch_.Get(D3D12NrScratchKind::Output);
            D3D12NrScratchKind nextKind = passOutput == scratch_.Get(D3D12NrScratchKind::Output)
                ? D3D12NrScratchKind::Output : D3D12NrScratchKind::PassScratch;
            if (scratch_.State(nextKind) == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                scratch_.Transition(cmd, nextKind, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }

    if (modelOk && finalAnswer != nullptr) {
        const bool targetUav = plan.cropColor ||
            (targetDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
        ID3D12Resource* resolveOriginal = acrossRr
            ? scratch_.Get(D3D12NrScratchKind::HdrCopy)
            : (targetUav ? scratch_.Get(D3D12NrScratchKind::HdrCopy) : activeTarget);
        ID3D12Resource* resolveTarget = acrossRr
            ? scratch_.Get(D3D12NrScratchKind::ResidualEdited)
            : (targetUav ? activeTarget : scratch_.Get(D3D12NrScratchKind::HdrCopy));

        if (acrossRr) {
            if (scratch_.State(D3D12NrScratchKind::ResidualEdited) ==
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                scratch_.Transition(cmd, D3D12NrScratchKind::ResidualEdited,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        } else if (targetUav) {
            if (plan.cropColor)
                scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            else
                Transition(cmd, activeTarget, activeState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        } else {
            scratch_.Transition(cmd, D3D12NrScratchKind::HdrCopy,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        D3D12NrCodecConstants resolve = ComposeConstants(
            request, D3D12NrCodecMode::Resolve, plan.activeColor.width, plan.activeColor.height);
        if (acrossRr) resolve.transferStrength = 1.0f;
        D3D12NrCodecResources r{};
        r.source = modelInput;
        r.model = finalAnswer;
        r.original = resolveOriginal;
        r.motion = motionIn;
        r.previousEdit = request.composition.useGameExposure ? resources.exposure : nullptr;
        r.target = resolveTarget;
        modelOk = codec_.Dispatch(cmd, resolve, r);

        if (modelOk && acrossRr) {
            scratch_.Transition(cmd, D3D12NrScratchKind::ResidualEdited,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            const D3D12NrScratchKind previous = residualHistoryIndex_ == 0
                ? D3D12NrScratchKind::ResidualHistory0 : D3D12NrScratchKind::ResidualHistory1;
            const D3D12NrScratchKind current = residualHistoryIndex_ == 0
                ? D3D12NrScratchKind::ResidualHistory1 : D3D12NrScratchKind::ResidualHistory0;
            if (scratch_.State(previous) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                scratch_.Transition(cmd, previous, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (scratch_.State(current) == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                scratch_.Transition(cmd, current, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            D3D12NrCodecConstants accum{};
            accum.mode = 0;
            accum.width = plan.activeColor.width;
            accum.height = plan.activeColor.height;
            accum.residualBlend = std::clamp(request.composition.residualBlend, 0.01f, 1.0f);
            accum.residualHistoryValid = residualHistoryPrimed_ ? 1u : 0u;
            accum.guideWidth = plan.motion.width;
            accum.guideHeight = plan.motion.height;
            accum.residualMotionBaseX = plan.motion.x;
            accum.residualMotionBaseY = plan.motion.y;
            accum.motionScaleX = request.motionScaleX / static_cast<float>(plan.activeColor.width);
            accum.motionScaleY = request.motionScaleY / static_cast<float>(plan.activeColor.height);
            D3D12NrCodecResources rr{};
            rr.source = scratch_.Get(D3D12NrScratchKind::HdrCopy);
            rr.model = scratch_.Get(D3D12NrScratchKind::ResidualEdited);
            rr.original = scratch_.Get(previous);
            rr.motion = motionIn;
            rr.target = scratch_.Get(current);
            modelOk = codec_.DispatchResidual(cmd, accum, rr);
            scratch_.Transition(cmd, current, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (modelOk) {
                residualHistoryIndex_ ^= 1u;
                residualHistoryPrimed_ = true;
                residualStoreValid_ = true;
                residualEpoch_ = request.submissionEpoch;
            }
        } else if (modelOk && !targetUav) {
            scratch_.Transition(cmd, D3D12NrScratchKind::HdrCopy,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_COPY_SOURCE);
            Transition(cmd, activeTarget, activeState, D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->CopyResource(activeTarget, scratch_.Get(D3D12NrScratchKind::HdrCopy));
            Transition(cmd, activeTarget, activeState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scratch_.Transition(cmd, D3D12NrScratchKind::HdrCopy,
                                D3D12_RESOURCE_STATE_COPY_SOURCE,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
    }

    for (D3D12NrScratchKind kind : {D3D12NrScratchKind::Output, D3D12NrScratchKind::PassScratch,
                                    D3D12NrScratchKind::ColorCopy, D3D12NrScratchKind::HdrCopy,
                                    D3D12NrScratchKind::ColorSmall}) {
        if (scratch_.Get(kind) != nullptr &&
            scratch_.State(kind) == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            scratch_.Transition(cmd, kind, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (TypedGuideFormat(resources.depth->GetDesc().Format) != resources.depth->GetDesc().Format)
        guideClones_.Transition(cmd, D3D12NrGuideKind::Depth,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_COPY_DEST);
    else
        Transition(cmd, resources.depth, depthState, request.depthState);
    if (TypedGuideFormat(resources.motion->GetDesc().Format) != resources.motion->GetDesc().Format)
        guideClones_.Transition(cmd, D3D12NrGuideKind::Motion,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_COPY_DEST);
    else
        Transition(cmd, resources.motion, motionState, request.motionState);

    if (plan.cropColor) {
        if (modelOk && !acrossRr) {
            scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                D3D12_RESOURCE_STATE_COPY_SOURCE);
            Transition(cmd, target, targetState, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12NrSubrect compact{0, 0, plan.activeColor.width, plan.activeColor.height};
            CopyRect(cmd, target, scratch_.Get(D3D12NrScratchKind::ActiveColor), compact);
            Transition(cmd, target, targetState, targetArrival);
            scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                D3D12_RESOURCE_STATE_COPY_SOURCE,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        } else if (scratch_.State(D3D12NrScratchKind::ActiveColor) ==
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
            scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    } else {
        Transition(cmd, target, activeState, targetArrival);
    }

    if (!modelOk) {
        residualStoreValid_ = false;
        return D3D12NrFrameResult::Failed;
    }
    return D3D12NrFrameResult::Applied;
}

} // namespace nrfusion
