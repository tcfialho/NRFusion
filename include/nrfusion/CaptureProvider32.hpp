#pragma once

#include "nrfusion/IpcProtocol.hpp"
#include "nrfusion/Types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace nrfusion {

struct CaptureClientConfig {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    float workingScale = 1.0f;
    bool isHdr = false;
    bool depthInverted = true;
    uint32_t colorFormat = 0;
    IpcProcessingMode processingMode = IpcProcessingMode::Neural;

    uint64_t colorSharedHandle = 0;
    uint64_t depthSharedHandle = 0;
    uint64_t motionSharedHandle = 0;
    uint64_t residualSharedHandle = 0;
    uint64_t producerFenceHandle = 0;
    uint64_t consumerFenceHandle = 0;
};

struct PipelinedFrameResult {
    bool hasResult = false;
    uint64_t readyWorkId = 0;
    uint64_t completedFenceValue = 0;
};

class CaptureProvider32 {
public:
    CaptureProvider32();
    ~CaptureProvider32();

    bool Connect(uint32_t hostPid, uint32_t timeoutMs = 3000);
    void Disconnect();
    bool IsConnected() const noexcept { return connected_; }

    bool Configure(const CaptureClientConfig& config);

    // Submits Frame N to the host asynchronously via overlapped IPC,
    // and returns immediately whether Frame N-1 is ready to be consumed.
    // Zero CPU blocking: uses overlapped non-blocking I/O.
    bool SubmitFramePipelined(uint64_t workId,
                             uint64_t producerFenceValue,
                             float jitterX, float jitterY,
                             bool reset,
                             PipelinedFrameResult& outResult);

    // The explicit form keeps WorkId, producer and consumer fence timelines independent.
    // Feature/View identify the work; neither is inferred from a frame number.
    bool SubmitFramePipelinedEx(uint64_t workId,
                                uint64_t featureId,
                                uint64_t viewId,
                                uint64_t producerFenceValue,
                                uint64_t consumerFenceValue,
                                float jitterX,
                                float jitterY,
                                bool reset,
                                PipelinedFrameResult& outResult);

    uint64_t ActiveSessionId() const noexcept { return sessionId_; }

private:
    bool DuplicateBuildHandlesForHost(const CaptureClientConfig& source, IpcBuildMessage& build) const;
    bool DuplicateOneHandleForHost(uint64_t sourceHandle, uint64_t& targetHandle) const;
    void CloseDuplicatedBuildHandles(const IpcBuildMessage& build) const;
    bool StartFrameAckRead();
    void ConsumeCompletedFrameAck();
    bool PollPendingWrites();
    void MarkTransportFailure();

    HANDLE pipeHandle_ = INVALID_HANDLE_VALUE;
    HANDLE hostProcess_ = nullptr;
    OVERLAPPED readOverlapped_{};
    OVERLAPPED writeOverlapped_{};
    HANDLE readEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    IpcFrameAckMessage pendingFrameAck_{};

    bool connected_ = false;
    bool frameAckReadPending_ = false;
    bool frameWritePending_ = false;
    uint64_t sessionId_ = 1;
    uint64_t lastSubmittedWorkId_ = 0;
    uint64_t lastCompletedWorkId_ = 0;
    uint64_t lastCompletedFenceValue_ = 0;
    uint64_t lastConsumedWorkId_ = 0;
    CaptureClientConfig config_{};
    mutable std::mutex mutex_;
};

} // namespace nrfusion
