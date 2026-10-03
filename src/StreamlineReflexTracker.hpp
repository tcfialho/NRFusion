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

    ReflexOwnershipInfo GetOwnershipInfo() const noexcept;
    ReflexValidationStats GetValidationStats() const noexcept;

    void Reset() noexcept;

private:
    StreamlineReflexTracker() noexcept = default;
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

    mutable std::mutex mutex_;
    sl::ReflexOptions lastGameRequested_{};
    sl::ReflexOptions lastEffective_{};
    bool haveGameRequested_ = false;
    bool mfgPromotedReflex_ = false;

    std::deque<FrameRecord> activeFrames_;
    std::uint32_t highestRetiredFrameId_ = 0;
    ReflexValidationStats stats_{};
};

} // namespace nrfusion::streamline
