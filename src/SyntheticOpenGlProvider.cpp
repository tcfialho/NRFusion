#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <iostream>
#include <limits>

// Only one compiler understands a library request written in the source. Elsewhere it is an
// unknown pragma, which a build with warnings as errors refuses outright.
#if defined(_MSC_VER)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "opengl32.lib")
#endif

namespace nrfusion {

SyntheticWorkHandle SyntheticOpenGlProvider::Submit(const SyntheticFrameInputs& inputs, void* /*commandList*/) {
    std::scoped_lock lock(mutex_);
    SyntheticWorkHandle handle{};
    if (!ready_ || !inputs.Valid()) return handle;

    if (!CreateSharedResources(inputs.renderResolution.width, inputs.renderResolution.height)) {
        return handle;
    }

    const uint32_t slotIdx =
        syntheticD3D12_.NextSlotForSubmit();
    if (slotIdx >= kMaxInFlight) return handle;
    const auto& selectedSlot = sharedSlots_[slotIdx];
    const bool released =
        selectedSlot.outputConsumed &&
        selectedSlot.sync.Valid(kMaxInFlight) &&
        selectedSlot.fence &&
        selectedSlot.fence->GetCompletedValue() >=
            selectedSlot.sync.releaseSignalValue;
    if (selectedSlot.inUse && !released) return handle;

    if (inputs.color.opaqueId >
        std::numeric_limits<GLuint>::max()) {
        return handle;
    }

    SharedSlot& slot = sharedSlots_[slotIdx];
    slot.sync = {};
    slot.innerSlot = SyntheticDx12Provider::kRingSlots;
    slot.inputRecorded = false;
    slot.outputPublished = false;
    slot.outputConsumed = false;
    slot.inUse = true;
    if (!ReserveOpenGlCarrierSyncIdentity(
            inputs.ticket.id, slotIdx, kMaxInFlight,
            slot.nextFenceValue, slot.sync)) {
        slot.inUse = false;
        return handle;
    }

    const GLuint gameColorTex =
        static_cast<GLuint>(inputs.color.opaqueId);
    if (!RecordOpenGlInputCopy(
            slot, gameColorTex,
            inputs.renderResolution.width,
            inputs.renderResolution.height)) {
        slot.inUse = false;
        slot.sync = {};
        return handle;
    }
    if (!slot.fence ||
        FAILED(d3d12Queue_->Wait(
            slot.fence.Get(), slot.sync.inputSignalValue))) {
        return handle;
    }

    SyntheticFrameInputs d12Inputs = inputs;
    d12Inputs.color.opaqueId = reinterpret_cast<uint64_t>(slot.d3d12Color.Get());
    d12Inputs.color.resolution = inputs.renderResolution;
    d12Inputs.color.format = ResourceFormat::Rgba16Float;

    if (FAILED(slot.alloc->Reset()) ||
        FAILED(d3d12CmdList_->Reset(slot.alloc.Get(), nullptr))) {
        return handle;
    }

    handle = syntheticD3D12_.Submit(d12Inputs, d3d12CmdList_.Get());
    if (!handle.valid || FAILED(d3d12CmdList_->Close())) {
        return SyntheticWorkHandle{};
    }
    slot.innerSlot = syntheticD3D12_.SlotForWork(handle.workId);
    if (slot.innerSlot != slotIdx) {
        return SyntheticWorkHandle{};
    }

    ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);

    handle.fenceValue = slot.sync.outputSignalValue;
    return handle;
}

bool SyntheticOpenGlProvider::Poll(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    if (!ready_ || !handle.valid || handle.workId == 0)
        return false;
    const SharedSlot* slot = FindSlot(handle);
    return slot && slot->outputPublished && slot->fence &&
           slot->fence->GetCompletedValue() >= handle.fenceValue;
}

ResourceRef SyntheticOpenGlProvider::GetResidual(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;

    SharedSlot* slot = FindSlot(handle);
    if (!slot || !slot->outputPublished) return ref;
    return syntheticD3D12_.GetResidual(handle);
}

bool SyntheticOpenGlProvider::ComposeNative(const SyntheticWorkHandle& handle,
                                           const ResourceRef& originalNative,
                                           const ResourceRef& destinationNative,
                                           void* commandList,
                                           float residualWeight) {
    return syntheticD3D12_.ComposeNative(handle, originalNative, destinationNative, commandList, residualWeight);
}

} // namespace nrfusion
