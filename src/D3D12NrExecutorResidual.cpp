#include "nrfusion/D3D12NrExecutor.hpp"

#include <algorithm>
#include <cmath>

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

} // namespace

D3D12NrFrameResult D3D12NrExecutor::ApplyStoredResidual(
    ID3D12GraphicsCommandList* cmd, const D3D12NrFrameResources& resources,
    const D3D12NrFrameRequest& request) {
    if (!residualStoreValid_ || request.submissionEpoch != residualEpoch_) {
        residualStoreValid_ = false;
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::SkippedPlacement;
    }
    residualStoreValid_ = false;

    const float strength = request.composition.transferStrength;
    if (!request.composition.applyModel || !std::isfinite(strength) || strength <= 0.0f) {
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::SkippedPlacement;
    }

    const D3D12_RESOURCE_DESC outputDesc = resources.output->GetDesc();
    if (outputDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        outputDesc.SampleDesc.Count != 1 || outputDesc.DepthOrArraySize != 1 ||
        outputDesc.MipLevels != 1 ||
        (outputDesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0) {
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::Failed;
    }

    ID3D12Device* device = nullptr;
    if (FAILED(resources.output->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) {
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::Failed;
    }
    const bool ready = codec_.Init(device) &&
        scratch_.EnsureOptional(
            device, D3D12NrScratchKind::ResidualComposed, outputDesc.Format,
            static_cast<std::uint32_t>(outputDesc.Width), outputDesc.Height, retirement_);
    device->Release();
    if (!ready) {
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::Failed;
    }

    ID3D12Resource* composed = scratch_.Get(D3D12NrScratchKind::ResidualComposed);
    const D3D12_RESOURCE_DESC carrierDesc = composed->GetDesc();
    if (carrierDesc.Width != outputDesc.Width || carrierDesc.Height != outputDesc.Height ||
        carrierDesc.Format != outputDesc.Format) {
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::Failed;
    }

    D3D12_RESOURCE_STATES outputState = request.outputState;
    const D3D12_RESOURCE_STATES carrierState =
        scratch_.State(D3D12NrScratchKind::ResidualComposed);
    if (!TransitionExternal(
            cmd, resources.output, outputState,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        (carrierState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
         !scratch_.Transition(
             cmd, D3D12NrScratchKind::ResidualComposed, carrierState,
             D3D12_RESOURCE_STATE_UNORDERED_ACCESS))) {
        residualHistoryPrimed_ = false;
        return D3D12NrFrameResult::Failed;
    }

    D3D12NrCodecConstants constants{};
    constants.mode = 1;
    constants.width = static_cast<std::uint32_t>(outputDesc.Width);
    constants.height = outputDesc.Height;
    constants.transferStrength = std::clamp(strength, 0.0f, 1.0f);

    D3D12NrCodecResources codecResources{};
    codecResources.source = resources.output;
    codecResources.model = scratch_.Get(
        residualHistoryIndex_ == 0
            ? D3D12NrScratchKind::ResidualHistory0
            : D3D12NrScratchKind::ResidualHistory1);
    codecResources.target = composed;

    const bool applied = codec_.DispatchResidual(cmd, constants, codecResources);
    if (applied) {
        scratch_.Transition(
            cmd, D3D12NrScratchKind::ResidualComposed,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransitionExternal(
            cmd, resources.output, outputState, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(resources.output, composed);
        TransitionExternal(cmd, resources.output, outputState, request.outputState);
        scratch_.Transition(
            cmd, D3D12NrScratchKind::ResidualComposed,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return D3D12NrFrameResult::Applied;
    }

    TransitionExternal(cmd, resources.output, outputState, request.outputState);
    scratch_.Transition(
        cmd, D3D12NrScratchKind::ResidualComposed,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    residualHistoryPrimed_ = false;
    return D3D12NrFrameResult::Failed;
}

} // namespace nrfusion
