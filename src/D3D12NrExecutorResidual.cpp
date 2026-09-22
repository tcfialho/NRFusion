#include "nrfusion/D3D12NrExecutor.hpp"

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
    if (!residualStoreValid_ || request.submissionEpoch != residualEpoch_)
        return D3D12NrFrameResult::SkippedPlacement;

    ID3D12Device* device = nullptr;
    if (FAILED(resources.output->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
        return D3D12NrFrameResult::Failed;
    const D3D12_RESOURCE_DESC outputDesc = resources.output->GetDesc();
    const bool ready = codec_.Init(device) &&
        scratch_.EnsureOptional(
            device, D3D12NrScratchKind::ResidualComposed, outputDesc.Format,
            static_cast<std::uint32_t>(outputDesc.Width), outputDesc.Height, retirement_);
    device->Release();
    if (!ready) return D3D12NrFrameResult::Failed;

    D3D12_RESOURCE_STATES outputState = request.outputState;
    const D3D12_RESOURCE_STATES composedState =
        scratch_.State(D3D12NrScratchKind::ResidualComposed);
    if (composedState != D3D12_RESOURCE_STATE_COPY_DEST &&
        !scratch_.Transition(
            cmd, D3D12NrScratchKind::ResidualComposed, composedState,
            D3D12_RESOURCE_STATE_COPY_DEST))
        return D3D12NrFrameResult::Failed;
    if (!TransitionExternal(
            cmd, resources.output, outputState, D3D12_RESOURCE_STATE_COPY_SOURCE))
        return D3D12NrFrameResult::Failed;

    cmd->CopyResource(
        scratch_.Get(D3D12NrScratchKind::ResidualComposed), resources.output);
    if (!scratch_.Transition(
            cmd, D3D12NrScratchKind::ResidualComposed,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !TransitionExternal(
            cmd, resources.output, outputState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
        return D3D12NrFrameResult::Failed;

    D3D12NrCodecConstants constants{};
    constants.mode = 1;
    constants.width = static_cast<std::uint32_t>(outputDesc.Width);
    constants.height = outputDesc.Height;
    constants.transferStrength = request.composition.transferStrength;

    D3D12NrCodecResources codecResources{};
    codecResources.source = scratch_.Get(D3D12NrScratchKind::ResidualComposed);
    codecResources.model = scratch_.Get(
        residualHistoryIndex_ == 0
            ? D3D12NrScratchKind::ResidualHistory0
            : D3D12NrScratchKind::ResidualHistory1);
    codecResources.target = resources.output;
    const bool applied = codec_.DispatchResidual(cmd, constants, codecResources);

    TransitionExternal(cmd, resources.output, outputState, request.outputState);
    scratch_.Transition(
        cmd, D3D12NrScratchKind::ResidualComposed,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    residualStoreValid_ = false;
    return applied ? D3D12NrFrameResult::Applied : D3D12NrFrameResult::Failed;
}

} // namespace nrfusion
