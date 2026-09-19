#include "nrfusion/CaptureProvider32.hpp"

#include <array>

namespace nrfusion {
namespace {

bool CompleteSetupIo(HANDLE pipe, OVERLAPPED& operation, BOOL started, DWORD& bytesTransferred) {
    if (started) return true;
    if (GetLastError() != ERROR_IO_PENDING) return false;
    return GetOverlappedResult(pipe, &operation, &bytesTransferred, TRUE) == TRUE;
}

} // namespace

CaptureProvider32::CaptureProvider32() {
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    readOverlapped_.hEvent = readEvent_;
    writeOverlapped_.hEvent = writeEvent_;
}

CaptureProvider32::~CaptureProvider32() {
    Disconnect();
    if (readEvent_) {
        CloseHandle(readEvent_);
        readEvent_ = nullptr;
    }
    if (writeEvent_) {
        CloseHandle(writeEvent_);
        writeEvent_ = nullptr;
    }
}

bool CaptureProvider32::Connect(uint32_t requestedPipePid, uint32_t timeoutMs) {
    std::scoped_lock lock(mutex_);
    if (connected_) return true;

    const uint32_t pipePid = requestedPipePid == 0 ? GetCurrentProcessId() : requestedPipePid;
    char pipeName[128]{};
    FormatPipeName(pipeName, sizeof(pipeName), pipePid);

    bool attemptedSpawn = false;
    const DWORD startTick = GetTickCount();
    while (true) {
        pipeHandle_ = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED, nullptr);
        if (pipeHandle_ != INVALID_HANDLE_VALUE) break;

        if (!attemptedSpawn && requestedPipePid == 0) {
            attemptedSpawn = true;
            const char* hostCandidates[] = {
                "OptiScaler\\NRFusion\\NRFusionHost64.exe",
                "NRFusionHost64.exe",
            };
            for (const char* candidate : hostCandidates) {
                if (GetFileAttributesA(candidate) == INVALID_FILE_ATTRIBUTES) continue;

                char commandLine[MAX_PATH + 32]{};
                snprintf(commandLine, sizeof(commandLine), "\"%s\" %u", candidate, pipePid);
                STARTUPINFOA startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION process{};
                if (CreateProcessA(nullptr, commandLine, nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr,
                                   &startup, &process)) {
                    CloseHandle(process.hProcess);
                    CloseHandle(process.hThread);
                    break;
                }
            }
        }

        if (GetLastError() == ERROR_PIPE_BUSY) {
            WaitNamedPipeA(pipeName, 50);
        }
        if (GetTickCount() - startTick >= timeoutMs) return false;
        Sleep(10);
    }

    IpcHelloMessage hello{};
    hello.clientPid = GetCurrentProcessId();
    hello.is32Bit = sizeof(void*) == 4 ? 1u : 0u;

    DWORD bytesWritten = 0;
    ResetEvent(writeEvent_);
    if (!CompleteSetupIo(pipeHandle_, writeOverlapped_,
                         WriteFile(pipeHandle_, &hello, sizeof(hello), &bytesWritten, &writeOverlapped_),
                         bytesWritten) || bytesWritten != sizeof(hello)) {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    IpcHelloAckMessage ack{};
    DWORD bytesRead = 0;
    ResetEvent(readEvent_);
    if (!CompleteSetupIo(pipeHandle_, readOverlapped_,
                         ReadFile(pipeHandle_, &ack, sizeof(ack), &bytesRead, &readOverlapped_),
                         bytesRead) || bytesRead != sizeof(ack) || ack.magic != NRFUSION_IPC_MAGIC ||
        ack.version != NRFUSION_IPC_VERSION || ack.status != 0 || ack.hostPid == 0) {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    hostProcess_ = OpenProcess(PROCESS_DUP_HANDLE, FALSE, ack.hostPid);
    if (!hostProcess_) {
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
        return false;
    }

    connected_ = true;
    frameAckReadPending_ = false;
    frameWritePending_ = false;
    lastSubmittedWorkId_ = 0;
    lastCompletedWorkId_ = 0;
    lastCompletedFenceValue_ = 0;
    lastConsumedWorkId_ = 0;
    return true;
}

void CaptureProvider32::Disconnect() {
    std::scoped_lock lock(mutex_);

    MarkTransportFailure();
}

void CaptureProvider32::MarkTransportFailure() {

    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(pipeHandle_, nullptr);
        CloseHandle(pipeHandle_);
        pipeHandle_ = INVALID_HANDLE_VALUE;
    }
    if (hostProcess_) {
        CloseHandle(hostProcess_);
        hostProcess_ = nullptr;
    }

    connected_ = false;
    frameAckReadPending_ = false;
    frameWritePending_ = false;
    lastSubmittedWorkId_ = 0;
    lastCompletedWorkId_ = 0;
    lastCompletedFenceValue_ = 0;
    lastConsumedWorkId_ = 0;
}

bool CaptureProvider32::DuplicateOneHandleForHost(uint64_t sourceHandle, uint64_t& targetHandle) const {
    targetHandle = 0;
    if (sourceHandle == 0) return true;
    if (!hostProcess_) return false;

    HANDLE duplicated = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(static_cast<uintptr_t>(sourceHandle)),
                         hostProcess_, &duplicated, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return false;
    }
    targetHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(duplicated));
    return true;
}

void CaptureProvider32::CloseDuplicatedBuildHandles(const IpcBuildMessage& build) const {
    if (!hostProcess_) return;

    const std::array<uint64_t, 6> handles = {
        build.colorSharedHandle,
        build.depthSharedHandle,
        build.motionSharedHandle,
        build.residualSharedHandle,
        build.producerFenceHandle,
        build.consumerFenceHandle,
    };
    for (uint64_t remoteValue : handles) {
        if (remoteValue == 0) continue;
        HANDLE localCopy = nullptr;
        if (DuplicateHandle(hostProcess_, reinterpret_cast<HANDLE>(static_cast<uintptr_t>(remoteValue)),
                            GetCurrentProcess(), &localCopy, 0, FALSE,
                            DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE) && localCopy) {
            CloseHandle(localCopy);
        }
    }
}

bool CaptureProvider32::DuplicateBuildHandlesForHost(const CaptureClientConfig& source,
                                                     IpcBuildMessage& build) const {
    return DuplicateOneHandleForHost(source.colorSharedHandle, build.colorSharedHandle) &&
           DuplicateOneHandleForHost(source.depthSharedHandle, build.depthSharedHandle) &&
           DuplicateOneHandleForHost(source.motionSharedHandle, build.motionSharedHandle) &&
           DuplicateOneHandleForHost(source.residualSharedHandle, build.residualSharedHandle) &&
           DuplicateOneHandleForHost(source.producerFenceHandle, build.producerFenceHandle) &&
           DuplicateOneHandleForHost(source.consumerFenceHandle, build.consumerFenceHandle);
}

bool CaptureProvider32::Configure(const CaptureClientConfig& config) {
    std::scoped_lock lock(mutex_);
    if (!connected_ || pipeHandle_ == INVALID_HANDLE_VALUE || !hostProcess_) return false;

    IpcBuildMessage build{};
    build.sessionId = ++sessionId_;
    build.width = config.width;
    build.height = config.height;
    build.targetWidth = config.targetWidth != 0 ? config.targetWidth : config.width;
    build.targetHeight = config.targetHeight != 0 ? config.targetHeight : config.height;
    build.workingScale = config.workingScale;
    build.isHdr = config.isHdr ? 1u : 0u;
    build.depthInverted = config.depthInverted ? 1u : 0u;
    build.colorFormat = config.colorFormat;
    build.processingMode = static_cast<uint32_t>(config.processingMode);

    if (!DuplicateBuildHandlesForHost(config, build)) {
        CloseDuplicatedBuildHandles(build);
        return false;
    }

    DWORD bytesWritten = 0;
    ResetEvent(writeEvent_);
    if (!CompleteSetupIo(pipeHandle_, writeOverlapped_,
                         WriteFile(pipeHandle_, &build, sizeof(build), &bytesWritten, &writeOverlapped_),
                         bytesWritten) || bytesWritten != sizeof(build)) {
        CloseDuplicatedBuildHandles(build);
        return false;
    }

    IpcBuildAckMessage ack{};
    DWORD bytesRead = 0;
    ResetEvent(readEvent_);
    if (!CompleteSetupIo(pipeHandle_, readOverlapped_,
                         ReadFile(pipeHandle_, &ack, sizeof(ack), &bytesRead, &readOverlapped_),
                         bytesRead) || bytesRead != sizeof(ack) || ack.magic != NRFUSION_IPC_MAGIC ||
        ack.version != NRFUSION_IPC_VERSION || ack.sessionId != build.sessionId || ack.status != 0) {
        return false;
    }

    config_ = config;
    frameAckReadPending_ = false;
    frameWritePending_ = false;
    lastSubmittedWorkId_ = 0;
    lastCompletedWorkId_ = 0;
    lastCompletedFenceValue_ = 0;
    lastConsumedWorkId_ = 0;
    return true;
}

bool CaptureProvider32::StartFrameAckRead() {
    if (frameAckReadPending_) return true;
    pendingFrameAck_ = {};

    DWORD bytesRead = 0;
    ResetEvent(readEvent_);
    if (ReadFile(pipeHandle_, &pendingFrameAck_, sizeof(pendingFrameAck_), &bytesRead, &readOverlapped_)) {
        if (bytesRead != sizeof(pendingFrameAck_)) return false;
        ConsumeCompletedFrameAck();
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        MarkTransportFailure();
        return false;
    }
    frameAckReadPending_ = true;
    return true;
}

void CaptureProvider32::ConsumeCompletedFrameAck() {
    if (pendingFrameAck_.magic != NRFUSION_IPC_MAGIC || pendingFrameAck_.version != NRFUSION_IPC_VERSION ||
        pendingFrameAck_.sessionId != sessionId_ ||
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
        return bytesWritten == sizeof(IpcFrameMessage);
    }
    if (GetLastError() == ERROR_IO_INCOMPLETE) return false;
    frameWritePending_ = false;
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
    if (!connected_ || pipeHandle_ == INVALID_HANDLE_VALUE || workId == 0 || producerFenceValue == 0 ||
        consumerFenceValue == 0) {
        return false;
    }

    if (!PollPendingWrites()) return false;

    if (frameAckReadPending_) {
        DWORD bytesRead = 0;
        if (GetOverlappedResult(pipeHandle_, &readOverlapped_, &bytesRead, FALSE)) {
            frameAckReadPending_ = false;
            if (bytesRead != sizeof(pendingFrameAck_)) return false;
            ConsumeCompletedFrameAck();
        } else if (GetLastError() != ERROR_IO_INCOMPLETE) {
            frameAckReadPending_ = false;
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
    } else if (bytesWritten != sizeof(message)) {
        return false;
    }

    lastSubmittedWorkId_ = workId;
    return StartFrameAckRead();
}

} // namespace nrfusion
