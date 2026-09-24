#include "nrfusion/CaptureProvider32.hpp"

#include <array>

namespace nrfusion {
namespace {

bool CompleteSetupIo(
    HANDLE pipe, OVERLAPPED& operation, BOOL started,
    DWORD& bytesTransferred, bool& pending, DWORD timeoutMs) {
    if (started) {
        pending = false;
        return true;
    }
    if (GetLastError() != ERROR_IO_PENDING) {
        pending = false;
        return false;
    }

    pending = true;
    if (WaitForSingleObject(operation.hEvent, timeoutMs) != WAIT_OBJECT_0)
        return false;

    const BOOL completed =
        GetOverlappedResult(pipe, &operation, &bytesTransferred, FALSE);
    pending = false;
    return completed == TRUE;
}

} // namespace

bool CaptureProvider32::Connect(uint32_t requestedPipePid, uint32_t timeoutMs) {
    std::scoped_lock lock(mutex_);
    if (connected_) return true;
    if (!DrainCanceledIo(0)) return false;
    ResetOverlappedState();

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

    auto remainingTimeout = [&]() -> DWORD {
        const DWORD elapsed = GetTickCount() - startTick;
        return elapsed >= timeoutMs ? 0u : timeoutMs - elapsed;
    };

    DWORD bytesWritten = 0;
    ResetEvent(writeEvent_);
    if (!CompleteSetupIo(
            pipeHandle_, writeOverlapped_,
            WriteFile(pipeHandle_, &hello, sizeof(hello), &bytesWritten, &writeOverlapped_),
            bytesWritten, writeIoPending_, remainingTimeout()) ||
        bytesWritten != sizeof(hello)) {
        MarkTransportFailure();
        return false;
    }

    IpcHelloAckMessage ack{};
    DWORD bytesRead = 0;
    ResetEvent(readEvent_);
    if (!CompleteSetupIo(
            pipeHandle_, readOverlapped_,
            ReadFile(pipeHandle_, &ack, sizeof(ack), &bytesRead, &readOverlapped_),
            bytesRead, readIoPending_, remainingTimeout()) ||
        bytesRead != sizeof(ack) || ack.magic != NRFUSION_IPC_MAGIC ||
        ack.version != NRFUSION_IPC_VERSION || ack.status != 0 || ack.hostPid == 0 ||
        ack.connectionGeneration == 0) {
        MarkTransportFailure();
        return false;
    }

    hostProcess_ = OpenProcess(PROCESS_DUP_HANDLE, FALSE, ack.hostPid);
    if (!hostProcess_) {
        MarkTransportFailure();
        return false;
    }

    connectionGeneration_ = ack.connectionGeneration;
    connected_ = true;
    frameAckReadPending_ = false;
    frameWritePending_ = false;
    lastSubmittedWorkId_ = 0;
    lastCompletedWorkId_ = 0;
    lastCompletedFenceValue_ = 0;
    lastConsumedWorkId_ = 0;
    return true;
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
    build.connectionGeneration = connectionGeneration_;
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

    constexpr DWORD kConfigureTimeoutMs = 3000;
    DWORD bytesWritten = 0;
    ResetEvent(writeEvent_);
    if (!CompleteSetupIo(
            pipeHandle_, writeOverlapped_,
            WriteFile(pipeHandle_, &build, sizeof(build), &bytesWritten, &writeOverlapped_),
            bytesWritten, writeIoPending_, kConfigureTimeoutMs) ||
        bytesWritten != sizeof(build)) {
        if (!writeIoPending_) CloseDuplicatedBuildHandles(build);
        MarkTransportFailure();
        return false;
    }

    IpcBuildAckMessage ack{};
    DWORD bytesRead = 0;
    ResetEvent(readEvent_);
    if (!CompleteSetupIo(
            pipeHandle_, readOverlapped_,
            ReadFile(pipeHandle_, &ack, sizeof(ack), &bytesRead, &readOverlapped_),
            bytesRead, readIoPending_, kConfigureTimeoutMs) ||
        bytesRead != sizeof(ack) || ack.magic != NRFUSION_IPC_MAGIC ||
        ack.version != NRFUSION_IPC_VERSION ||
        !IpcSessionMatches(connectionGeneration_, build.sessionId,
                           ack.connectionGeneration, ack.sessionId) ||
        ack.status != 0) {
        MarkTransportFailure();
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

} // namespace nrfusion
