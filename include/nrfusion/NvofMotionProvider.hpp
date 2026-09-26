#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include "nrfusion/Types.hpp"

#include <cstdint>
#include <mutex>

namespace nrfusion {

// Fail-closed placeholder for a future NVIDIA Optical Flow executor.
// DLL/hardware discovery alone is never guide evidence.
class NvofMotionProvider {
public:
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

    NvofMotionProvider() = default;
    ~NvofMotionProvider();

    bool Initialize(
        ID3D12Device* device,
        ID3D12CommandQueue* computeQueue,
        uint32_t fullWidth,
        uint32_t fullHeight);
    void Shutdown();
    void ResetHistory();

    bool Submit(
        ID3D12Resource* source,
        uint64_t sourceSequence,
        bool reset,
        ID3D12GraphicsCommandList* cmdList,
        Submission& outSubmission);

    bool IsComplete(const Submission& submission) const noexcept;
    ID3D12Fence* CompletionFence() const noexcept { return nullptr; }
    bool LastSubmitWasBackpressured() const noexcept { return false; }

    Resolution FlowResolution() const noexcept { return flowRes_; }
    bool IsReady() const noexcept { return ready_; }

private:
    static Resolution ComputeFlowResolution(
        uint32_t fullWidth,
        uint32_t fullHeight) noexcept;

    Resolution fullRes_{};
    Resolution flowRes_{};
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
