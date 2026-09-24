#include "nrfusion/CaptureProvider32.hpp"

namespace nrfusion {

bool CaptureProvider32::StartFrameAckRead() {
    if (frameAckReadPending_) return true;
    pendingFrameAck_ = {};

    DWORD bytesRead = 0;
    ResetEvent(readEvent_);
    if (ReadFile(pipeHandle_, &pendingFrameAck_, sizeof(pendingFrameAck_), &bytesRead, &readOverlapped_)) {
        readIoPending_ = false;
        if (bytesRead != sizeof(pendingFrameAck_)) {
            MarkTransportFailure();
            return false;
        }
        ConsumeCompletedFrameAck();
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        MarkTransportFailure();
        return false;
    }
    frameAckReadPending_ = true;
    readIoPending_ = true;
    return true;
}

void CaptureProvider32::ConsumeCompletedFrameAck() {
    if (pendingFrameAck_.magic != NRFUSION_IPC_MAGIC || pendingFrameAck_.version != NRFUSION_IPC_VERSION ||
        !IpcSessionMatches(connectionGeneration_, sessionId_,
                           pendingFrameAck_.connectionGeneration,
                           pendingFrameAck_.sessionId) ||
        pendingFrameAck_.status != static_cast<uint32_t>(IpcFrameStatus::Complete)) {
        return;
    }
    lastCompletedWorkId_ = pendingFrameAck_.workId;
    lastCompletedFenceValue_ = pendingFrameAck_.completedFenceValue;
}

bool CaptureProvider32::PollPendingWrites() {
    if (!frameWritePending_) return true;

    DWORD bytesWritten = 0;
    if (GetOverlappedResult(pipeHandle_, &writeOverlapped_, &bytesWritten, FALSE)) {
        frameWritePending_ = false;
        writeIoPending_ = false;
        return bytesWritten == sizeof(IpcFrameMessage);
    }
    if (GetLastError() == ERROR_IO_INCOMPLETE) return false;
    frameWritePending_ = false;
    writeIoPending_ = false;
    MarkTransportFailure();
    return false;
}

bool CaptureProvider32::SubmitFramePipelined(uint64_t workId,
                                             uint64_t producerFenceValue,
                                             float jitterX,
                                             float jitterY,
                                             bool reset,
                                             PipelinedFrameResult& outResult) {
    return SubmitFramePipelinedEx(workId, 0, 0, producerFenceValue, producerFenceValue,
                                  jitterX, jitterY, reset, outResult);
}

bool CaptureProvider32::SubmitFramePipelinedEx(uint64_t workId,
                                               uint64_t featureId,
                                               uint64_t viewId,
                                               uint64_t producerFenceValue,
                                               uint64_t consumerFenceValue,
                                               float jitterX,
                                               float jitterY,
                                               bool reset,
                                               PipelinedFrameResult& outResult) {
    std::scoped_lock lock(mutex_);
    outResult = {};
    if (!connected_ || connectionGeneration_ == 0 || pipeHandle_ == INVALID_HANDLE_VALUE ||
        workId == 0 || producerFenceValue == 0 ||
        consumerFenceValue == 0) {
        return false;
    }

    if (!PollPendingWrites()) return false;

    if (frameAckReadPending_) {
        DWORD bytesRead = 0;
        if (GetOverlappedResult(pipeHandle_, &readOverlapped_, &bytesRead, FALSE)) {
            frameAckReadPending_ = false;
            readIoPending_ = false;
            if (bytesRead != sizeof(pendingFrameAck_)) {
                MarkTransportFailure();
                return false;
            }
            ConsumeCompletedFrameAck();
        } else if (GetLastError() != ERROR_IO_INCOMPLETE) {
            frameAckReadPending_ = false;
            readIoPending_ = false;
            MarkTransportFailure();
            return false;
        }
    } else if (!StartFrameAckRead()) {
        return false;
    }

    // A result observed before this submission is consumable. Results that complete after this
    // point are left for the next Present, preserving N/N-1 instead of a same-frame CPU wait.
    if (lastCompletedWorkId_ != 0 && lastCompletedWorkId_ != lastConsumedWorkId_) {
        outResult.hasResult = true;
        outResult.readyWorkId = lastCompletedWorkId_;
        outResult.completedFenceValue = lastCompletedFenceValue_;
        lastConsumedWorkId_ = lastCompletedWorkId_;
    }

    IpcFrameMessage message{};
    message.sessionId = sessionId_;
    message.connectionGeneration = connectionGeneration_;
    message.workId = workId;
    message.featureId = featureId;
    message.viewId = viewId;
    message.producerFenceValue = producerFenceValue;
    message.consumerFenceValue = consumerFenceValue;
    message.jitterX = jitterX;
    message.jitterY = jitterY;
    message.reset = reset ? 1u : 0u;

    DWORD bytesWritten = 0;
    ResetEvent(writeEvent_);
    if (!WriteFile(pipeHandle_, &message, sizeof(message), &bytesWritten, &writeOverlapped_)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            MarkTransportFailure();
            return false;
        }
        frameWritePending_ = true;
        writeIoPending_ = true;
    } else if (bytesWritten != sizeof(message)) {
        MarkTransportFailure();
        return false;
    }

    lastSubmittedWorkId_ = workId;
    return StartFrameAckRead();
}

} // namespace nrfusion
