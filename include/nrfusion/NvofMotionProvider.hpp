#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/Types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace nrfusion {

using Microsoft::WRL::ComPtr;

class NvofMotionProvider {
public:
    static constexpr uint32_t kSlotCount = 5;
    static constexpr uint32_t kTargetFlowHeight = 180;

    struct Submission {
        bool valid = false;
        uint32_t slot = ~0u;
        uint64_t completionValue = 0;
        ID3D12Resource* motion = nullptr;
        ID3D12Resource* historyMask = nullptr;
        Resolution flowResolution{};
        float scaleX = 1.0f;
        float scaleY = 1.0f;
    };

    NvofMotionProvider();
    ~NvofMotionProvider();

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* computeQueue,
                    uint32_t fullWidth, uint32_t fullHeight);
    void Shutdown();
    void ResetHistory();

    // Submits the frame to the low-res optical flow queue asynchronously.
    // Zero CPU wait: queue wait and dispatch are entirely GPU-driven.
    bool Submit(ID3D12Resource* source,
                uint64_t sourceSequence,
                bool reset,
                ID3D12GraphicsCommandList* cmdList,
                Submission& outSubmission);

    bool IsComplete(const Submission& submission) const;
    ID3D12Fence* CompletionFence() const { return completionFence_.Get(); }
    bool LastSubmitWasBackpressured() const noexcept { return backpressured_; }

    Resolution FlowResolution() const noexcept { return flowRes_; }
    bool IsReady() const noexcept { return ready_; }

private:
    struct Slot {
        uint64_t sequence = 0;
        uint64_t fenceValue = 0;
        ComPtr<ID3D12Resource> lowLuma;
        ComPtr<ID3D12Resource> forwardFlow;
        ComPtr<ID3D12Resource> backwardFlow;
        ComPtr<ID3D12Resource> guideMotion;
        ComPtr<ID3D12Resource> historyMask;
        bool inUse = false;
    };

    bool EnsureResources();
    static Resolution ComputeFlowResolution(uint32_t fullWidth, uint32_t fullHeight) noexcept;

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> completionFence_;
    uint64_t nextFenceValue_ = 1;

    Resolution fullRes_{};
    Resolution flowRes_{};
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;

    std::array<Slot, kSlotCount> ringSlots_{};
    uint32_t currentSlot_ = 0;
    bool ready_ = false;
    bool backpressured_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
