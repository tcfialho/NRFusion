#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace nrfusion {

#define NRFUSION_IPC_MAGIC   0x4E524655u  // 'NRFU'
#define NRFUSION_IPC_VERSION 3u

// v3 adds an explicit connection generation so stale traffic cannot cross reconnect boundaries.
// Client and host must agree on the version before interpreting any handle value.
enum class IpcProcessingMode : uint32_t {
    DummyCopy = 0,
    Neural = 1,
};

enum class IpcFrameStatus : uint32_t {
    Complete = 0,
    DroppedBackpressure = 1,
    InvalidResources = 2,
};

enum class IpcMessageKind : uint32_t {
    Hello = 1,
    Build = 2,
    BuildAck = 3,
    Frame = 4,
    FrameAck = 5,
    Shutdown = 6
};

#pragma pack(push, 8)

struct IpcHelloMessage {
    uint32_t magic = NRFUSION_IPC_MAGIC;
    uint32_t version = NRFUSION_IPC_VERSION;
    uint32_t clientPid = 0;
    uint32_t is32Bit = 1;
};

struct IpcHelloAckMessage {
    uint32_t magic = NRFUSION_IPC_MAGIC;
    uint32_t version = NRFUSION_IPC_VERSION;
    uint32_t hostPid = 0;
    uint32_t status = 0; // 0 = OK
    uint64_t connectionGeneration = 0;
};

struct IpcBuildMessage {
    uint32_t magic = NRFUSION_IPC_MAGIC;
    uint32_t version = NRFUSION_IPC_VERSION;
    uint64_t sessionId = 0;
    uint64_t connectionGeneration = 0;

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t targetWidth = 0;
    uint32_t targetHeight = 0;
    float workingScale = 1.0f;
    uint32_t isHdr = 0;
    uint32_t depthInverted = 1;
    uint32_t colorFormat = 0;
    uint32_t processingMode = static_cast<uint32_t>(IpcProcessingMode::Neural);

    // NT shared handles (passed as 64-bit unsigned ints for cross-bitness safety)
    uint64_t colorSharedHandle = 0;
    uint64_t depthSharedHandle = 0;
    uint64_t motionSharedHandle = 0;
    uint64_t residualSharedHandle = 0;
    uint64_t producerFenceHandle = 0;
    uint64_t consumerFenceHandle = 0;
};

struct IpcBuildAckMessage {
    uint32_t magic = NRFUSION_IPC_MAGIC;
    uint32_t version = NRFUSION_IPC_VERSION;
    uint64_t sessionId = 0;
    uint64_t connectionGeneration = 0;
    uint32_t status = 0; // 0 = Success
    uint32_t workWidth = 0;
    uint32_t workHeight = 0;
};

struct IpcFrameMessage {
    uint32_t magic = NRFUSION_IPC_MAGIC;
    uint32_t version = NRFUSION_IPC_VERSION;
    uint64_t sessionId = 0;
    uint64_t connectionGeneration = 0;

    uint64_t workId = 0;
    uint64_t featureId = 0;
    uint64_t viewId = 0;

    uint64_t producerFenceValue = 0;
    uint64_t consumerFenceValue = 0;

    float jitterX = 0.0f;
    float jitterY = 0.0f;
    float preExposure = 1.0f;

    uint32_t reset = 0;
    uint32_t cameraCut = 0;
};

struct IpcFrameAckMessage {
    uint32_t magic = NRFUSION_IPC_MAGIC;
    uint32_t version = NRFUSION_IPC_VERSION;
    uint64_t sessionId = 0;
    uint64_t connectionGeneration = 0;
    uint64_t workId = 0;
    uint64_t completedFenceValue = 0;
    uint32_t status = static_cast<uint32_t>(IpcFrameStatus::Complete);
};

#pragma pack(pop)

constexpr bool IpcConnectionMatches(
    uint64_t expectedGeneration, uint64_t actualGeneration) noexcept {
    return expectedGeneration != 0 && expectedGeneration == actualGeneration;
}

constexpr bool IpcSessionMatches(
    uint64_t expectedGeneration, uint64_t expectedSession,
    uint64_t actualGeneration, uint64_t actualSession) noexcept {
    return IpcConnectionMatches(expectedGeneration, actualGeneration) &&
           expectedSession != 0 && expectedSession == actualSession;
}

// Maximum in-flight frames allowed over IPC
constexpr uint32_t kIpcMaxInFlight = 3;

inline void FormatPipeName(char* outBuffer, size_t bufferSize, uint32_t pid) {
    snprintf(outBuffer, bufferSize, "\\\\.\\pipe\\nrfusion-ipc.%u", static_cast<unsigned int>(pid));
}

} // namespace nrfusion
