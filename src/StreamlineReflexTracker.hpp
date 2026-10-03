#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "nrfusion/StreamlineDlssgHook.hpp"
#include <sl_core_types.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <cstdint>
#include <deque>
#include <mutex>

namespace nrfusion::streamline {

enum class FrameStage : std::uint32_t {
    None = 0,
    Sleep = 1,
    SimulationStart = 2,
    InputSample = 3,
    SimulationEnd = 4,
    RenderSubmitStart = 5,
    RenderSubmitEnd = 6,
    PresentStart = 7,
    PresentEnd = 8
};

class StreamlineReflexTracker {
public:
    static StreamlineReflexTracker& Instance() noexcept;

    sl::ReflexOptions OnGameReflexSetOptions(const sl::ReflexOptions& requested, bool mfgActive);
    bool OnMfgStateChanged(bool mfgActive, sl::ReflexOptions& outOptions);

    void RecordSleep(const sl::FrameToken& frame);
    void RecordMarker(sl::PCLMarker marker, const sl::FrameToken& frame);
    void SetLatencyMode(MfgLatencyMode mode, std::uint32_t targetDisplayFps = 0, std::uint32_t targetNativeFps = 0) noexcept;
    MfgLatencyMode GetLatencyMode() const noexcept;
    std::uint32_t GetTargetDisplayFps() const noexcept;
    std::uint32_t GetTargetNativeFps() const noexcept;
    void SetAutoPerformance(bool enabled) noexcept;
    bool IsAutoPerformance() const noexcept;
    void SetMfgMultiplier(std::uint32_t multiplier) noexcept;
    bool ApplyCurrentLatencyPolicy(bool mfgActive, sl::ReflexOptions& outOptions);

    void SetPacerMode(MfgPacerMode mode) noexcept;
    MfgPacerMode GetPacerMode() const noexcept;

    ReflexOwnershipInfo GetOwnershipInfo() const noexcept;
    ReflexValidationStats GetValidationStats() const noexcept;

    void Reset() noexcept;

private:
    StreamlineReflexTracker() noexcept;
    ~StreamlineReflexTracker() noexcept = default;

    struct FrameRecord {
        std::uint32_t frameId = 0;
        const void* tokenPtr = nullptr;
        std::uint32_t sleepCount = 0;
        std::uint64_t firstSleepTick = 0;
        std::uint32_t lastStage = 0;
        std::uint32_t stageMask = 0;
        bool duplicateSleep = false;
        bool mixedToken = false;
        bool orderViolation = false;
        bool completed = false;
    };

    FrameRecord& GetOrCreateFrameRecordLocked(std::uint32_t frameId, const void* token);
    void CompleteFrameRecordLocked(FrameRecord& record);
    sl::ReflexOptions CalculateEffectiveOptionsLocked(bool mfgActive) const;

    mutable std::mutex mutex_;
    sl::ReflexOptions lastGameRequested_{};
    sl::ReflexOptions lastEffective_{};
    bool haveGameRequested_ = false;
    bool mfgPromotedReflex_ = false;
    MfgLatencyMode latencyMode_ = MfgLatencyMode::LowLatency;
    MfgPacerMode pacerMode_ = MfgPacerMode::Auto;
    bool isAutoPerformance_ = true;
    std::uint32_t targetDisplayFps_ = 60;
    std::uint32_t targetNativeFps_ = 30;
    std::uint32_t mfgMultiplier_ = 2;
    std::uint64_t lastFrameQpc_ = 0;
    float measuredNativeFps_ = 0.0f;

    std::deque<FrameRecord> activeFrames_;
    std::uint32_t highestRetiredFrameId_ = 0;
    ReflexValidationStats stats_{};
};

} // namespace nrfusion::streamline
