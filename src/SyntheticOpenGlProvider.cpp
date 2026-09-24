#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <iostream>

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
        if (candidateSlot.producerFenceValue == 0 ||
            completed >= candidateSlot.producerFenceValue) {
            slotIdx = candidate;
            break;
        }
    }
    if (slotIdx == kMaxInFlight) return handle;

    currentSlot_ = (slotIdx + 1) % kMaxInFlight;
    SharedSlot& slot = sharedSlots_[slotIdx];
    slot.workId = inputs.ticket.id;
    slot.inUse = true;

    uint64_t fVal = nextFenceValue_++;
    slot.producerFenceValue = fVal;

    SyntheticFrameInputs d12Inputs = inputs;
    d12Inputs.color.opaqueId = reinterpret_cast<uint64_t>(slot.d3d12Color.Get());
    d12Inputs.color.resolution = inputs.renderResolution;
    d12Inputs.color.format = ResourceFormat::Rgba16Float;

    slot.alloc->Reset();
    d3d12CmdList_->Reset(slot.alloc.Get(), nullptr);

    handle = syntheticD3D12_.Submit(d12Inputs, d3d12CmdList_.Get());

    d3d12CmdList_->Close();
    ID3D12CommandList* lists[] = { d3d12CmdList_.Get() };
    d3d12Queue_->ExecuteCommandLists(1, lists);
    d3d12Queue_->Signal(d3d12Fence_.Get(), fVal);

    handle.fenceValue = fVal;
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
