#include "nrfusion/SyntheticOpenGlProvider.hpp"

namespace nrfusion {

std::optional<OpenGlD3D12WorkView>
SyntheticOpenGlProvider::GetD3D12Work(
    const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    if (!ready_) return std::nullopt;

    SharedSlot* slot = FindSlot(handle);
    if (!slot || !slot->inputRecorded ||
        slot->outputPublished ||
        slot->innerSlot >= SyntheticDx12Provider::kRingSlots ||
        syntheticD3D12_.SlotForWork(handle.workId) !=
            slot->innerSlot) {
        return std::nullopt;
    }

    OpenGlD3D12WorkView view{};
    view.device = d3d12Device_.Get();
    view.queue = d3d12Queue_.Get();
    view.inputColor =
        syntheticD3D12_.GetSlotLowColor(slot->innerSlot);
    view.neuralOutput =
        syntheticD3D12_.GetSlotLowNeuralOut(slot->innerSlot);
    view.workingResolution = handle.workResolution;
    if (!view.Valid()) return std::nullopt;
    return view;
}

bool SyntheticOpenGlProvider::PublishD3D12Result(
    const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    if (!ready_) return false;

    SharedSlot* slot = FindSlot(handle);
    if (!slot || !slot->inputRecorded ||
        slot->outputPublished || slot->outputConsumed ||
        slot->innerSlot >= SyntheticDx12Provider::kRingSlots ||
        syntheticD3D12_.SlotForWork(handle.workId) !=
            slot->innerSlot ||
        !slot->publishAlloc || !slot->fence ||
        !slot->d3d12Color || !slot->d3d12Output ||
        !d3d12CmdList_ || !d3d12Queue_) {
        return false;
    }

    if (FAILED(slot->publishAlloc->Reset()) ||
        FAILED(d3d12CmdList_->Reset(
            slot->publishAlloc.Get(), nullptr))) {
        return false;
    }

    ResourceRef original{};
    original.opaqueId =
        reinterpret_cast<std::uint64_t>(slot->d3d12Color.Get());
    original.resolution = currentRes_;
    original.format = ResourceFormat::Rgba16Float;

    ResourceRef destination{};
    destination.opaqueId =
        reinterpret_cast<std::uint64_t>(slot->d3d12Output.Get());
    destination.resolution = currentRes_;
    destination.format = ResourceFormat::Rgba16Float;

    const bool recorded =
        syntheticD3D12_.ExtractResidual(
            handle, d3d12CmdList_.Get()) &&
        syntheticD3D12_.ComposeNative(
            handle, original, destination,
            d3d12CmdList_.Get(), 1.0f);
    if (!recorded) {
        d3d12CmdList_->Close();
        return false;
    }
    if (FAILED(d3d12CmdList_->Close())) return false;

    ID3D12CommandList* lists[] = {d3d12CmdList_.Get()};
    d3d12Queue_->ExecuteCommandLists(1, lists);
    if (FAILED(d3d12Queue_->Signal(
            slot->fence.Get(),
            slot->sync.outputSignalValue))) {
        return false;
    }

    slot->outputPublished = true;
    return true;
}

} // namespace nrfusion
