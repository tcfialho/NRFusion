#include "nrfusion/HostServer64.hpp"

#include <fstream>
#include <iostream>

namespace nrfusion {
namespace {

template <typename Interface>
bool OpenAndReleaseTargetHandle(ID3D12Device* device, uint64_t rawHandle,
                                Microsoft::WRL::ComPtr<Interface>& destination) {
    if (rawHandle == 0) return true;

    HANDLE targetHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(rawHandle));
    const bool opened = device != nullptr &&
        SUCCEEDED(device->OpenSharedHandle(targetHandle, IID_PPV_ARGS(&destination)));
    CloseHandle(targetHandle);
    return opened;
}


} // namespace

HostServer64::HostServer64() {
    eventHandle_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    overlapped_.hEvent = eventHandle_;
}

HostServer64::~HostServer64() {
    Stop();
    if (eventHandle_) {
        CloseHandle(eventHandle_);
        eventHandle_ = nullptr;
    }
}

void HostServer64::ServerLoop() {
    while (running_) {
        ResetEvent(eventHandle_);
        BOOL connResult = ConnectNamedPipe(pipeHandle_, &overlapped_);
        if (!connResult) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                while (running_) {
                    DWORD waitRes = WaitForSingleObject(eventHandle_, 50);
                    if (waitRes == WAIT_OBJECT_0) break;
                }
                if (!running_) break;
            } else if (err == ERROR_PIPE_CONNECTED) {
                // Client already connected before ConnectNamedPipe
            } else {
                break;
            }
        }

        clientConnected_ = true;

        auto readExact = [this](void* buffer, DWORD size) -> bool {
            DWORD bytesRead = 0;
            ResetEvent(eventHandle_);
            BOOL ok = ReadFile(pipeHandle_, buffer, size, &bytesRead, &overlapped_);
            if (!ok) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    while (running_) {
                        DWORD waitRes = WaitForSingleObject(eventHandle_, 50);
                        if (waitRes == WAIT_OBJECT_0) break;
                    }
                    if (!running_) return false;
                    if (!GetOverlappedResult(pipeHandle_, &overlapped_, &bytesRead, FALSE)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            return (bytesRead == size);
        };

        auto writeExact = [this](const void* buffer, DWORD size) -> bool {
            DWORD bytesWritten = 0;
            ResetEvent(eventHandle_);
            BOOL ok = WriteFile(pipeHandle_, buffer, size, &bytesWritten, &overlapped_);
            if (!ok) {
                if (GetLastError() == ERROR_IO_PENDING) {
                    while (running_) {
                        DWORD waitRes = WaitForSingleObject(eventHandle_, 50);
                        if (waitRes == WAIT_OBJECT_0) break;
                    }
                    if (!running_) return false;
                    if (!GetOverlappedResult(pipeHandle_, &overlapped_, &bytesWritten, FALSE)) {
                        return false;
                    }
                } else {
                    return false;
                }
            }
            return (bytesWritten == size);
        };

        // 1. Handshake: Hello
        IpcHelloMessage hello{};
        if (!readExact(&hello, sizeof(hello)) || hello.magic != NRFUSION_IPC_MAGIC ||
            hello.version != NRFUSION_IPC_VERSION) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        const uint64_t connectionGeneration = nextConnectionGeneration_++;
        IpcHelloAckMessage helloAck{};
        helloAck.magic = NRFUSION_IPC_MAGIC;
        helloAck.version = NRFUSION_IPC_VERSION;
        helloAck.hostPid = GetCurrentProcessId();
        helloAck.status = 0;
        helloAck.connectionGeneration = connectionGeneration;
        if (!writeExact(&helloAck, sizeof(helloAck))) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        // 2. Build configuration
        IpcBuildMessage build{};
        if (!readExact(&build, sizeof(build)) || build.magic != NRFUSION_IPC_MAGIC ||
            build.version != NRFUSION_IPC_VERSION || build.sessionId == 0 ||
            !IpcConnectionMatches(connectionGeneration, build.connectionGeneration)) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        // These handles were duplicated into this process by CaptureProvider32. They are one-shot
        // transport handles: after OpenSharedHandle succeeds or fails, this host must close them.
        // Keeping the raw client values would only work in an in-process test, not across x86/x64.
        importedColor_.Reset();
        importedResidual_.Reset();
        importedDepth_.Reset();
        importedMotion_.Reset();
        importedProducerFence_.Reset();
        importedConsumerFence_.Reset();

        const bool carriesSharedTransport = build.colorSharedHandle != 0 || build.residualSharedHandle != 0 ||
            build.producerFenceHandle != 0 || build.consumerFenceHandle != 0 ||
            build.depthSharedHandle != 0 || build.motionSharedHandle != 0;
        const bool modeKnown = build.processingMode == static_cast<uint32_t>(IpcProcessingMode::DummyCopy) ||
            build.processingMode == static_cast<uint32_t>(IpcProcessingMode::Neural);

        bool handlesOpened = !carriesSharedTransport || InitializeD3D12();
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.colorSharedHandle, importedColor_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.residualSharedHandle, importedResidual_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.depthSharedHandle, importedDepth_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.motionSharedHandle, importedMotion_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.producerFenceHandle, importedProducerFence_);
        handlesOpened &= OpenAndReleaseTargetHandle(d3d12Device_.Get(), build.consumerFenceHandle, importedConsumerFence_);

        const bool transportComplete = !carriesSharedTransport ||
            (importedColor_ && importedResidual_ && importedProducerFence_ && importedConsumerFence_);
        currentBuild_ = build;

        IpcBuildAckMessage buildAck{};
        buildAck.magic = NRFUSION_IPC_MAGIC;
        buildAck.version = NRFUSION_IPC_VERSION;
        buildAck.sessionId = build.sessionId;
        buildAck.connectionGeneration = connectionGeneration;
        const bool dummyRequiresTransport = build.processingMode == static_cast<uint32_t>(IpcProcessingMode::DummyCopy);
        buildAck.status = handlesOpened && transportComplete && modeKnown &&
            (!dummyRequiresTransport || carriesSharedTransport) ? 0u : 1u;
        buildAck.workWidth = static_cast<uint32_t>(build.width * build.workingScale);
        buildAck.workHeight = static_cast<uint32_t>(build.height * build.workingScale);
        if (!writeExact(&buildAck, sizeof(buildAck))) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }
        if (buildAck.status != 0) {
            DisconnectNamedPipe(pipeHandle_);
            clientConnected_ = false;
            continue;
        }

        // 3. Pipelined Frame streaming loop
        while (running_ && clientConnected_) {
            IpcFrameMessage frameMsg{};
            if (!readExact(&frameMsg, sizeof(frameMsg))) {
                // Client disconnected or pipe broken
                break;
            }

            if (frameMsg.magic != NRFUSION_IPC_MAGIC || frameMsg.version != NRFUSION_IPC_VERSION ||
                !IpcSessionMatches(currentBuild_.connectionGeneration,
                                   currentBuild_.sessionId,
                                   frameMsg.connectionGeneration,
                                   frameMsg.sessionId)) {
                break;
            }

            const IpcFrameAckMessage frameAck = ProcessFrame(frameMsg);

            if (!writeExact(&frameAck, sizeof(frameAck))) {
                break;
            }
        }

        DisconnectNamedPipe(pipeHandle_);
        clientConnected_ = false;
        importedColor_.Reset();
        importedResidual_.Reset();
        importedDepth_.Reset();
        importedMotion_.Reset();
        importedProducerFence_.Reset();
        importedConsumerFence_.Reset();
    }
}

} // namespace nrfusion
