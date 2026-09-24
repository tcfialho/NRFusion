#pragma once

#include "nrfusion/HostDlssNr.hpp"
#include "nrfusion/IpcProtocol.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nrfusion {

class HostServer64 {
public:
    HostServer64();
    ~HostServer64();

    bool Start(uint32_t hostPid);
    void Stop();
    bool IsRunning() const noexcept { return running_; }
    bool IsClientConnected() const noexcept { return clientConnected_; }

    uint64_t ProcessedFrameCount() const noexcept { return processedFrames_; }
    uint64_t LastProcessedWorkId() const noexcept { return lastWorkId_; }
    // Frames where dlssnr_call_evaluate_v2 actually returned success, not just frames the
    // downsample step touched -- the signal a caller needs to tell "ran" from "worked".
    uint64_t DlssNrEvaluatedFrameCount() const noexcept { return dlssNrEvaluatedFrames_; }
    uint64_t DlssNrAttemptedFrameCount() const noexcept { return dlssNrAttemptedFrames_; }
    std::string DlssNrStatus() const { return dlssNr_ ? dlssNr_->Status() : "not initialised"; }

    bool InitializeD3D12();

private:
    void ServerLoop();
    IpcFrameAckMessage ProcessFrame(const IpcFrameMessage& frameMsg);
    bool EnsureZeroGuides(uint32_t width, uint32_t height);
    void CollectRetiredTransport();
    void RetireImportedTransport();

    HANDLE pipeHandle_ = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped_{};
    HANDLE eventHandle_ = nullptr;

    std::atomic<bool> running_{ false };
    std::atomic<bool> clientConnected_{ false };
    std::atomic<uint64_t> processedFrames_{ 0 };
    std::atomic<uint64_t> lastWorkId_{ 0 };
    std::atomic<uint64_t> dlssNrEvaluatedFrames_{ 0 };
    std::atomic<uint64_t> dlssNrAttemptedFrames_{ 0 };

    std::thread workerThread_;
    IpcBuildMessage currentBuild_{};
    uint64_t nextConnectionGeneration_ = 0;

    ComPtr<ID3D12Device> d3d12Device_;
    ComPtr<ID3D12CommandQueue> d3d12Queue_;
    ComPtr<ID3D12CommandAllocator> d3d12Alloc_;
    std::array<ComPtr<ID3D12CommandAllocator>, kIpcMaxInFlight> d3d12Allocs_{};
    std::array<uint64_t, kIpcMaxInFlight> allocFenceValues_{};
    uint32_t currentAllocSlot_ = 0;
    ComPtr<ID3D12GraphicsCommandList> d3d12CmdList_;
    ComPtr<ID3D12Fence> d3d12Fence_;
    uint64_t fenceValue_ = 0;

    ComPtr<ID3D12Resource> importedColor_;
    ComPtr<ID3D12Resource> importedResidual_;
    ComPtr<ID3D12Resource> importedDepth_;
    ComPtr<ID3D12Resource> importedMotion_;
    ComPtr<ID3D12Fence> importedProducerFence_;
    ComPtr<ID3D12Fence> importedConsumerFence_;
    uint64_t importedTransportFenceValue_ = 0;

    struct RetiredTransport {
        ComPtr<ID3D12Resource> color;
        ComPtr<ID3D12Resource> residual;
        ComPtr<ID3D12Resource> depth;
        ComPtr<ID3D12Resource> motion;
        ComPtr<ID3D12Fence> producerFence;
        ComPtr<ID3D12Fence> consumerFence;
        uint64_t fenceValue = 0;
    };
    std::deque<RetiredTransport> retiredTransports_;
    std::unique_ptr<SyntheticDx12Provider> syntheticProvider_;

    // Missing depth/motion inputs use zero-filled guides. Imported guides, when present, bypass
    // these resources; the fallback costs quality but preserves transport correctness.
    ComPtr<ID3D12Resource> zeroGuideUpload_;
    ComPtr<ID3D12Resource> lowGuideDepth_;
    ComPtr<ID3D12Resource> lowGuideMotion_;
    uint32_t guideWidth_ = 0;
    uint32_t guideHeight_ = 0;
    // Its own allocator/list/fence: rebuilding a guide is rare (once, or on a resolution change),
    // and giving it a lane separate from the per-frame list means the steady-state frame loop never
    // pays a synchronous wait for a step that almost never runs.
    ComPtr<ID3D12CommandAllocator> guideAlloc_;
    ComPtr<ID3D12GraphicsCommandList> guideCmdList_;
    ComPtr<ID3D12Fence> guideFence_;
    uint64_t guideFenceValue_ = 0;
    uint64_t guideUseFenceValue_ = 0;
    std::unique_ptr<HostDlssNr> dlssNr_;
};

} // namespace nrfusion
