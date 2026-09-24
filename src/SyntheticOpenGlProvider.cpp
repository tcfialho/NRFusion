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

    uint32_t slotIdx = kMaxInFlight;
    const uint64_t completed = d3d12Fence_->GetCompletedValue();
    for (uint32_t offset = 0; offset != kMaxInFlight; ++offset) {
        const uint32_t candidate =
            (currentSlot_ + offset) % kMaxInFlight;
        const auto& candidateSlot = sharedSlots_[candidate];
        const bool released =
            candidateSlot.outputConsumed &&
            candidateSlot.sync.Valid(kMaxInFlight) &&
            completed >= candidateSlot.sync.releaseSignalValue;
        if (!candidateSlot.inUse || released) {
            slotIdx = candidate;
            break;
        }
    }
    if (slotIdx == kMaxInFlight) return handle;

    if (inputs.color.opaqueId >
        std::numeric_limits<GLuint>::max()) {
        return handle;
    }

    currentSlot_ = (slotIdx + 1) % kMaxInFlight;
    SharedSlot& slot = sharedSlots_[slotIdx];
    slot.sync = {};
    slot.inputRecorded = false;
    slot.outputConsumed = false;
    slot.inUse = true;
    if (!ReserveOpenGlCarrierSyncIdentity(
            inputs.ticket.id, slotIdx, kMaxInFlight,
            nextFenceValue_, slot.sync)) {
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
    if (FAILED(d3d12Queue_->Wait(
            d3d12Fence_.Get(), slot.sync.inputSignalValue))) {
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

    ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);
    if (FAILED(d3d12Queue_->Signal(
            d3d12Fence_.Get(), slot.sync.outputSignalValue))) {
        return SyntheticWorkHandle{};
    }

    handle.fenceValue = slot.sync.outputSignalValue;
    return handle;
}

bool SyntheticOpenGlProvider::Poll(const SyntheticWorkHandle& handle) {
    if (!ready_ || !handle.valid || !d3d12Fence_) return false;
    return d3d12Fence_->GetCompletedValue() >= handle.fenceValue;
}

ResourceRef SyntheticOpenGlProvider::GetResidual(const SyntheticWorkHandle& handle) {
    std::scoped_lock lock(mutex_);
    ResourceRef ref{};
    if (!ready_ || !handle.valid) return ref;

    for (const auto& s : sharedSlots_) {
        if (s.workId == handle.workId && s.d3d12Residual) {
            ref.opaqueId = reinterpret_cast<uint64_t>(s.d3d12Residual.Get());
            ref.resolution = currentRes_;
            ref.format = ResourceFormat::Rgba16Float;
            return ref;
        }
    }
    return ref;
}

bool SyntheticOpenGlProvider::ComposeNative(const SyntheticWorkHandle& handle,
                                           const ResourceRef& originalNative,
                                           const ResourceRef& destinationNative,
                                           void* commandList,
                                           float residualWeight) {
    return syntheticD3D12_.ComposeNative(handle, originalNative, destinationNative, commandList, residualWeight);
}

} // namespace nrfusion
