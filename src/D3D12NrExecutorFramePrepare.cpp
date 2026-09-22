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

void CopyFromSubrect(ID3D12GraphicsCommandList* cmd, ID3D12Resource* dst,
                     ID3D12Resource* src, const D3D12NrSubrect& rect) noexcept {
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = src;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION target = source;
    target.pResource = dst;
    D3D12_BOX box{rect.x, rect.y, 0, rect.x + rect.width, rect.y + rect.height, 1};
    cmd->CopyTextureRegion(&target, 0, 0, 0, &source, &box);
}

D3D12NrCodecConstants EncodeConstants(
    const D3D12NrFrameRequest& request, std::uint32_t width,
    std::uint32_t height) noexcept {
    D3D12NrCodecConstants c{};
    c.mode = static_cast<std::uint32_t>(D3D12NrCodecMode::Encode);
    c.whitePoint = std::max(request.composition.whitePoint, 0.01f);
    c.width = width;
    c.height = height;
    c.passthrough = request.composition.colourIsLinearHdr ? 0u : 1u;
    c.reversibleMode = request.composition.reversibleMode;
    c.useGameExposure = request.composition.useGameExposure ? 1u : 0u;
    c.exposurePreMul = request.composition.exposurePreMul;
    return c;
}

} // namespace

bool D3D12NrExecutor::PrepareFrameResources(
    ID3D12GraphicsCommandList* cmd, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request, FrameContext& context) noexcept {
    context.activeTarget = context.target;
    if (context.cropColor) {
        if (!TransitionExternal(cmd, context.target, context.targetState,
                                D3D12_RESOURCE_STATE_COPY_SOURCE) ||
            !scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_DEST))
            return false;
        CopyFromSubrect(
            cmd, scratch_.Get(D3D12NrScratchKind::ActiveColor),
            context.target, context.plan.activeColor);
        if (!TransitionExternal(cmd, context.target, context.targetState,
                                context.targetArrival) ||
            !scratch_.Transition(cmd, D3D12NrScratchKind::ActiveColor,
                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
        context.activeTarget = scratch_.Get(D3D12NrScratchKind::ActiveColor);
    } else if (!TransitionExternal(
                   cmd, context.target, context.targetState,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
        return false;
    }

    if (request.composition.useGameExposure) {
        if (resources.exposure == nullptr ||
            !TransitionExternal(cmd, resources.exposure, context.exposureState,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
    }

    D3D12NrCodecResources encodeResources{};
    encodeResources.source = context.activeTarget;
    encodeResources.previousEdit =
        request.composition.useGameExposure ? resources.exposure : nullptr;
    encodeResources.target = scratch_.Get(D3D12NrScratchKind::ColorCopy);
    encodeResources.keep = scratch_.Get(D3D12NrScratchKind::HdrCopy);
    if (!codec_.Dispatch(
            cmd, EncodeConstants(request, context.plan.activeColor.width,
                                 context.plan.activeColor.height),
            encodeResources))
        return false;
    if (!scratch_.Transition(
            cmd, D3D12NrScratchKind::ColorCopy,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !scratch_.Transition(
            cmd, D3D12NrScratchKind::HdrCopy,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
        return false;

    context.modelInput = scratch_.Get(D3D12NrScratchKind::ColorCopy);
    if (context.plan.reduced) {
        D3D12NrCodecConstants scale{};
        scale.mode = static_cast<std::uint32_t>(D3D12NrCodecMode::Downsample);
        scale.width = context.plan.work.width;
        scale.height = context.plan.work.height;
        D3D12NrCodecResources scaleResources{};
        scaleResources.source = context.modelInput;
        scaleResources.target = scratch_.Get(D3D12NrScratchKind::ColorSmall);
        if (!codec_.Dispatch(cmd, scale, scaleResources) ||
            !scratch_.Transition(
                cmd, D3D12NrScratchKind::ColorSmall,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return false;
        context.modelInput = scratch_.Get(D3D12NrScratchKind::ColorSmall);
    }

    auto prepareGuide = [&](D3D12NrGuideKind kind, ID3D12Resource* source,
                            D3D12_RESOURCE_STATES& state,
                            D3D12_RESOURCE_STATES arrival,
                            bool& cloned) -> ID3D12Resource* {
        const D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
        const DXGI_FORMAT typed = TypedGuideFormat(sourceDesc.Format);
        if (typed == sourceDesc.Format)
            return TransitionExternal(
                cmd, source, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
                ? source : nullptr;

        ID3D12Device* device = nullptr;
        if (FAILED(source->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
            return nullptr;
        const bool ensured = guideClones_.Ensure(device, kind, source, typed, retirement_);
        device->Release();
        if (!ensured || !TransitionExternal(
                cmd, source, state, D3D12_RESOURCE_STATE_COPY_SOURCE))
            return nullptr;
        cmd->CopyResource(guideClones_.Get(kind), source);
        if (!TransitionExternal(cmd, source, state, arrival) ||
            !guideClones_.Transition(
                cmd, kind, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
            return nullptr;
        cloned = true;
        return guideClones_.Get(kind);
    };

    context.depthIn = prepareGuide(
        D3D12NrGuideKind::Depth, resources.depth, context.depthState,
        request.depthState, context.depthCloned);
    context.motionIn = prepareGuide(
        D3D12NrGuideKind::Motion, resources.motion, context.motionState,
        request.motionState, context.motionCloned);
    return context.depthIn != nullptr && context.motionIn != nullptr;
}

void D3D12NrExecutor::RestoreFrameResources(
    ID3D12GraphicsCommandList* cmd, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request, FrameContext& context) noexcept {
    for (D3D12NrScratchKind kind : {
             D3D12NrScratchKind::Output, D3D12NrScratchKind::PassScratch,
             D3D12NrScratchKind::ColorCopy, D3D12NrScratchKind::HdrCopy,
             D3D12NrScratchKind::ColorSmall, D3D12NrScratchKind::ActiveColor}) {
        if (scratch_.Get(kind) == nullptr) continue;
        const D3D12_RESOURCE_STATES state = scratch_.State(kind);
        if (state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
            scratch_.Transition(cmd, kind, state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (context.depthCloned) {
        if (guideClones_.State(D3D12NrGuideKind::Depth) ==
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            guideClones_.Transition(
                cmd, D3D12NrGuideKind::Depth,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);
    } else {
        TransitionExternal(cmd, resources.depth, context.depthState, request.depthState);
    }

    if (context.motionCloned) {
        if (guideClones_.State(D3D12NrGuideKind::Motion) ==
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
            guideClones_.Transition(
                cmd, D3D12NrGuideKind::Motion,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_COPY_DEST);
    } else {
        TransitionExternal(cmd, resources.motion, context.motionState, request.motionState);
    }

    if (request.composition.useGameExposure && resources.exposure != nullptr)
        TransitionExternal(
            cmd, resources.exposure, context.exposureState, request.exposureState);
    TransitionExternal(cmd, context.target, context.targetState, context.targetArrival);
}

} // namespace nrfusion
