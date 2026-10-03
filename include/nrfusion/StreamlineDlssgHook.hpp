#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>
#include "nrfusion/RuntimeConfig.hpp"

namespace nrfusion {

struct StreamlineMfgStatus {
    bool linked = false;
    bool nativeDynamicSupported = false;
    bool updatePending = false;
    std::uint32_t optionsVersion = 0;
    std::uint32_t maxGeneratedFrames = 0;
    std::uint32_t lastResult = 0;
    std::uint32_t runtimeStatus = 0;
    bool dynamicActive = false;
    std::uint32_t framesPresentedInSample = 0;
    bool reflexLinked = false;
    bool markersActive = false;
    std::uint32_t markerCount = 0;
    std::uint32_t queueParallelismMode = 0;
};

struct ReflexOwnershipInfo {
    bool haveGameOptions = false;
    std::uint32_t gameMode = 0;
    std::uint32_t gameFrameLimitUs = 0;
    bool gameUseMarkersToOptimize = false;
    std::uint16_t gameVirtualKey = 0;
    std::uint32_t gameIdThread = 0;

    std::uint32_t effectiveMode = 0;
    std::uint32_t effectiveFrameLimitUs = 0;
    bool effectiveUseMarkersToOptimize = false;
    std::uint16_t effectiveVirtualKey = 0;
    std::uint32_t effectiveIdThread = 0;

    bool mfgPromotedReflex = false;
    std::uint32_t latencyMode = 0;
    std::uint32_t targetDisplayFps = 0;
    std::uint32_t targetNativeFps = 0;
    bool autoPerformance = false;
};

struct ReflexValidationStats {
    std::uint64_t framesAnalyzed = 0;
    std::uint64_t missingSleeps = 0;
    std::uint64_t duplicateSleeps = 0;
    std::uint64_t mixedTokens = 0;
    std::uint64_t staleTokens = 0;
    std::uint64_t orderViolations = 0;
    std::uint64_t missingMarkerFrames = 0;
    std::uint64_t perfectFrames = 0;
};

class StreamlineDlssgHook {
public:
    static StreamlineDlssgHook& Instance() noexcept;

    bool Install(HMODULE interposerModule = nullptr) noexcept;
    void TriggerLiveMultiplierUpdate() noexcept;
    StreamlineMfgStatus Status() const noexcept;

    ReflexOwnershipInfo ReflexOwnership() const noexcept;
    ReflexValidationStats ReflexStats() const noexcept;
    void ResetReflexValidation() noexcept;

    void SetMfgLatencyMode(MfgLatencyMode mode, std::uint32_t targetDisplayFps = 0, std::uint32_t targetNativeFps = 0) noexcept;
    MfgLatencyMode GetMfgLatencyMode() const noexcept;
    std::uint32_t GetTargetDisplayFps() const noexcept;
    std::uint32_t GetTargetNativeFps() const noexcept;

    void SetMfgTargetAutoPerformance(bool autoPerformance) noexcept;
    bool IsAutoPerformance() const noexcept;

    void SetMfgPacerMode(MfgPacerMode mode) noexcept;
    MfgPacerMode GetMfgPacerMode() const noexcept;

    bool IsInstalled() const noexcept { return installed_.load(); }

private:
    StreamlineDlssgHook() noexcept = default;
    ~StreamlineDlssgHook() noexcept = default;

    StreamlineDlssgHook(const StreamlineDlssgHook&) = delete;
    StreamlineDlssgHook& operator=(const StreamlineDlssgHook&) = delete;

    std::atomic<bool> installed_{false};
};

} // namespace nrfusion
