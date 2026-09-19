#include "nrfusion/NvofMotionProvider.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion {

NvofMotionProvider::NvofMotionProvider() = default;

NvofMotionProvider::~NvofMotionProvider() {
    Shutdown();
}

Resolution NvofMotionProvider::ComputeFlowResolution(uint32_t fullWidth, uint32_t fullHeight) noexcept {
    if (fullWidth == 0 || fullHeight == 0) return {};
    uint32_t fh = kTargetFlowHeight;
    uint32_t fw = static_cast<uint32_t>(std::round(static_cast<float>(fullWidth) * static_cast<float>(fh) / static_cast<float>(fullHeight)));
    // Even dimensions
    fw = (fw + 1) & ~1u;
    fh = (fh + 1) & ~1u;
    return { fw, fh };
}

bool NvofMotionProvider::Initialize(ID3D12Device* device, ID3D12CommandQueue* computeQueue,
                                    uint32_t fullWidth, uint32_t fullHeight) {
    std::scoped_lock lock(mutex_);
    if (ready_) return true;
    if (!device) return false;

    device_ = device;
    queue_ = computeQueue;
    fullRes_ = { fullWidth, fullHeight };
    flowRes_ = ComputeFlowResolution(fullWidth, fullHeight);

    if (!flowRes_.Valid()) return false;

    scaleX_ = static_cast<float>(fullWidth) / static_cast<float>(flowRes_.width);
    scaleY_ = static_cast<float>(fullHeight) / static_cast<float>(flowRes_.height);

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&completionFence_)))) {
        return false;
    }

    if (!EnsureResources()) {
        return false;
    }

    ready_ = true;
    return true;
}

void NvofMotionProvider::Shutdown() {
    std::scoped_lock lock(mutex_);
    if (!ready_) return;

    for (auto& s : ringSlots_) {
        s.lowLuma.Reset();
        s.forwardFlow.Reset();
        s.backwardFlow.Reset();
        s.guideMotion.Reset();
        s.historyMask.Reset();
        s.inUse = false;
        s.sequence = 0;
        s.fenceValue = 0;
    }

    completionFence_.Reset();
    queue_.Reset();
    device_.Reset();
    ready_ = false;
}

void NvofMotionProvider::ResetHistory() {
    std::scoped_lock lock(mutex_);
    for (auto& s : ringSlots_) {
        s.inUse = false;
        s.sequence = 0;
    }
}

bool NvofMotionProvider::EnsureResources() {
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC lumaDesc{};
    lumaDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    lumaDesc.Width = flowRes_.width;
    lumaDesc.Height = flowRes_.height;
    lumaDesc.DepthOrArraySize = 1;
    lumaDesc.MipLevels = 1;
    lumaDesc.Format = DXGI_FORMAT_R8_UNORM;
    lumaDesc.SampleDesc.Count = 1;
    lumaDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_RESOURCE_DESC motionDesc = lumaDesc;
    motionDesc.Format = DXGI_FORMAT_R16G16_FLOAT;

    D3D12_RESOURCE_DESC maskDesc = lumaDesc;
    maskDesc.Format = DXGI_FORMAT_R8_UNORM;

    for (auto& s : ringSlots_) {
        if (FAILED(device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &lumaDesc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   IID_PPV_ARGS(&s.lowLuma)))) return false;

        if (FAILED(device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &motionDesc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   IID_PPV_ARGS(&s.guideMotion)))) return false;

        if (FAILED(device_->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &maskDesc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   IID_PPV_ARGS(&s.historyMask)))) return false;
    }

    return true;
}

bool NvofMotionProvider::Submit(ID3D12Resource* source,
                                uint64_t sourceSequence,
                                bool reset,
                                ID3D12GraphicsCommandList* cmdList,
                                Submission& outSubmission) {
    // History is dropped through ResetHistory(), which callers already use, so the flag here has no
    // work left to do. It stays in the signature because callers still pass it.
    (void)reset;

    std::scoped_lock lock(mutex_);
    if (!ready_ || !source || !cmdList) {
        outSubmission.valid = false;
        return false;
    }

    uint32_t slotIdx = currentSlot_;
    currentSlot_ = (currentSlot_ + 1) % kSlotCount;
    Slot& slot = ringSlots_[slotIdx];

    // Check backpressure on slot ring
    if (slot.inUse && completionFence_) {
        uint64_t completed = completionFence_->GetCompletedValue();
        if (completed < slot.fenceValue) {
            backpressured_ = true;
        } else {
            backpressured_ = false;
        }
    }

    slot.inUse = true;
    slot.sequence = sourceSequence;
    uint64_t fVal = nextFenceValue_++;
    slot.fenceValue = fVal;

    // Asynchronous recording on command list without CPU blocking
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = slot.guideMotion.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &b);

    // Transition to read state after recording
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmdList->ResourceBarrier(1, &b);

    outSubmission.valid = true;
    outSubmission.slot = slotIdx;
    outSubmission.completionValue = fVal;
    outSubmission.motion = slot.guideMotion.Get();
    outSubmission.historyMask = slot.historyMask.Get();
    outSubmission.flowResolution = flowRes_;
    outSubmission.scaleX = scaleX_;
    outSubmission.scaleY = scaleY_;

    return true;
}

bool NvofMotionProvider::IsComplete(const Submission& submission) const {
    if (!ready_ || !submission.valid || !completionFence_) return false;
    return completionFence_->GetCompletedValue() >= submission.completionValue;
}

} // namespace nrfusion
