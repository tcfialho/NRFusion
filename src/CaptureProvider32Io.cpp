#include "nrfusion/CaptureProvider32.hpp"

namespace nrfusion {

CaptureProvider32::CaptureProvider32() {
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ResetOverlappedState();
}

CaptureProvider32::~CaptureProvider32() {
    Disconnect();
    DrainCanceledIo(1000);
    if (readEvent_) {
        CloseHandle(readEvent_);
        readEvent_ = nullptr;
    }
    if (writeEvent_) {
        CloseHandle(writeEvent_);
        writeEvent_ = nullptr;
    }
}

void CaptureProvider32::Disconnect() {
    std::scoped_lock lock(mutex_);

    MarkTransportFailure();
}

void CaptureProvider32::ResetOverlappedState() {
    readOverlapped_ = {};
    writeOverlapped_ = {};
    readOverlapped_.hEvent = readEvent_;
    writeOverlapped_.hEvent = writeEvent_;
    if (readEvent_) ResetEvent(readEvent_);
    if (writeEvent_) ResetEvent(writeEvent_);
}

bool CaptureProvider32::DrainCanceledIo(uint32_t timeoutMs) {
    if (!ioRetiring_) return true;

    const DWORD start = GetTickCount();
    auto drainOne = [&](OVERLAPPED& operation, HANDLE event, bool& pending) {
        if (!pending) return true;
        const DWORD elapsed = GetTickCount() - start;
        const DWORD waitMs = timeoutMs == 0 ? 0 :
            (elapsed >= timeoutMs ? 0 : timeoutMs - elapsed);
        if (WaitForSingleObject(event, waitMs) != WAIT_OBJECT_0) return false;

        DWORD ignored = 0;
        GetOverlappedResult(pipeHandle_, &operation, &ignored, FALSE);
        pending = false;
        return true;
    };

    if (!drainOne(readOverlapped_, readEvent_, readIoPending_) ||
        !drainOne(writeOverlapped_, writeEvent_, writeIoPending_)) {
        return false;
    }

    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
    }
    ioRetiring_ = false;
    ResetOverlappedState();
    return true;
}

void CaptureProvider32::MarkTransportFailure() {
    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        if (readIoPending_ || writeIoPending_) {
            CancelIoEx(pipeHandle_, nullptr);
            ioRetiring_ = true;
            DrainCanceledIo(0);
        } else {
            CloseHandle(pipeHandle_);
            pipeHandle_ = INVALID_HANDLE_VALUE;
        }
    }
    if (hostProcess_) {
        CloseHandle(hostProcess_);
        hostProcess_ = nullptr;
    }

    connected_ = false;
    connectionGeneration_ = 0;
    frameAckReadPending_ = false;
    frameWritePending_ = false;
    lastSubmittedWorkId_ = 0;
    lastCompletedWorkId_ = 0;
    lastCompletedFenceValue_ = 0;
    lastConsumedWorkId_ = 0;
}

} // namespace nrfusion
