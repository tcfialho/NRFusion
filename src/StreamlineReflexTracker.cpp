#include "StreamlineReflexTracker.hpp"
#include <algorithm>

namespace nrfusion::streamline {

StreamlineReflexTracker& StreamlineReflexTracker::Instance() noexcept {
    static StreamlineReflexTracker instance;
    return instance;
}

sl::ReflexOptions StreamlineReflexTracker::OnGameReflexSetOptions(
    const sl::ReflexOptions& requested, bool mfgActive) {
    std::lock_guard lock(mutex_);
    lastGameRequested_ = requested;
    haveGameRequested_ = true;

    sl::ReflexOptions applied = requested;
    if (mfgActive) {
        if (requested.mode == sl::ReflexMode::eOff) {
            applied.mode = sl::ReflexMode::eLowLatency;
            mfgPromotedReflex_ = true;
        } else {
            applied.mode = requested.mode;
            mfgPromotedReflex_ = false;
        }
    } else {
        applied.mode = requested.mode;
        mfgPromotedReflex_ = false;
    }
    lastEffective_ = applied;
    return applied;
}

bool StreamlineReflexTracker::OnMfgStateChanged(bool mfgActive, sl::ReflexOptions& outOptions) {
    std::lock_guard lock(mutex_);
    if (!haveGameRequested_) return false;

    if (mfgActive) {
        if (lastGameRequested_.mode == sl::ReflexMode::eOff) {
            if (!mfgPromotedReflex_) {
                outOptions = lastGameRequested_;
                outOptions.mode = sl::ReflexMode::eLowLatency;
                mfgPromotedReflex_ = true;
                lastEffective_ = outOptions;
                return true;
            }
        } else {
            mfgPromotedReflex_ = false;
        }
    } else {
        if (mfgPromotedReflex_) {
            outOptions = lastGameRequested_;
            mfgPromotedReflex_ = false;
            lastEffective_ = outOptions;
            return true;
        }
    }
    return false;
}

void StreamlineReflexTracker::RecordSleep(const sl::FrameToken& frame) {
    std::lock_guard lock(mutex_);
    const auto fid = static_cast<std::uint32_t>(frame);
    const void* token = &frame;
    auto& rec = GetOrCreateFrameRecordLocked(fid, token);

    rec.sleepCount++;
    if (rec.sleepCount > 1) {
        rec.duplicateSleep = true;
        stats_.duplicateSleeps++;
    } else {
        rec.firstSleepTick = GetTickCount64();
    }

    const auto sleepStage = static_cast<std::uint32_t>(FrameStage::Sleep);
    if (rec.lastStage > sleepStage) {
        rec.orderViolation = true;
        stats_.orderViolations++;
    }
    rec.lastStage = std::max(rec.lastStage, sleepStage);
    rec.stageMask |= (1u << sleepStage);
}

void StreamlineReflexTracker::RecordMarker(sl::PCLMarker marker, const sl::FrameToken& frame) {
    std::lock_guard lock(mutex_);
    const auto fid = static_cast<std::uint32_t>(frame);
    const void* token = &frame;

    FrameStage stage = FrameStage::None;
    const auto rawMarker = static_cast<std::uint32_t>(marker);
    if (rawMarker == 6 || marker == sl::PCLMarker::eControllerInputSample) {
        stage = FrameStage::InputSample;
    } else if (marker == sl::PCLMarker::eSimulationStart) {
        stage = FrameStage::SimulationStart;
    } else if (marker == sl::PCLMarker::eSimulationEnd) {
        stage = FrameStage::SimulationEnd;
    } else if (marker == sl::PCLMarker::eRenderSubmitStart) {
        stage = FrameStage::RenderSubmitStart;
    } else if (marker == sl::PCLMarker::eRenderSubmitEnd) {
        stage = FrameStage::RenderSubmitEnd;
    } else if (marker == sl::PCLMarker::ePresentStart) {
        stage = FrameStage::PresentStart;
    } else if (marker == sl::PCLMarker::ePresentEnd) {
        stage = FrameStage::PresentEnd;
    }

    if (stage == FrameStage::None) return;

    auto& rec = GetOrCreateFrameRecordLocked(fid, token);
    const auto currentStage = static_cast<std::uint32_t>(stage);

    if (currentStage < rec.lastStage) {
        rec.orderViolation = true;
        stats_.orderViolations++;
    }
    rec.lastStage = std::max(rec.lastStage, currentStage);
    rec.stageMask |= (1u << currentStage);

    if (stage == FrameStage::PresentEnd) {
        CompleteFrameRecordLocked(rec);
    }
}

ReflexOwnershipInfo StreamlineReflexTracker::GetOwnershipInfo() const noexcept {
    std::lock_guard lock(mutex_);
    ReflexOwnershipInfo info{};
    info.haveGameOptions = haveGameRequested_;
    info.gameMode = static_cast<std::uint32_t>(lastGameRequested_.mode);
    info.gameFrameLimitUs = lastGameRequested_.frameLimitUs;
    info.gameUseMarkersToOptimize = lastGameRequested_.useMarkersToOptimize;
    info.gameVirtualKey = lastGameRequested_.virtualKey;
    info.gameIdThread = lastGameRequested_.idThread;

    info.effectiveMode = static_cast<std::uint32_t>(lastEffective_.mode);
    info.effectiveFrameLimitUs = lastEffective_.frameLimitUs;
    info.effectiveUseMarkersToOptimize = lastEffective_.useMarkersToOptimize;
    info.effectiveVirtualKey = lastEffective_.virtualKey;
    info.effectiveIdThread = lastEffective_.idThread;
    info.mfgPromotedReflex = mfgPromotedReflex_;
    return info;
}

ReflexValidationStats StreamlineReflexTracker::GetValidationStats() const noexcept {
    std::lock_guard lock(mutex_);
    return stats_;
}

void StreamlineReflexTracker::Reset() noexcept {
    std::lock_guard lock(mutex_);
    lastGameRequested_ = {};
    lastEffective_ = {};
    haveGameRequested_ = false;
    mfgPromotedReflex_ = false;
    activeFrames_.clear();
    highestRetiredFrameId_ = 0;
    stats_ = {};
}

StreamlineReflexTracker::FrameRecord& StreamlineReflexTracker::GetOrCreateFrameRecordLocked(
    std::uint32_t frameId, const void* token) {
    for (auto& rec : activeFrames_) {
        if (rec.frameId == frameId) {
            if (rec.tokenPtr != nullptr && token != nullptr && rec.tokenPtr != token) {
                if (!rec.mixedToken) {
                    rec.mixedToken = true;
                    stats_.mixedTokens++;
                }
            }
            return rec;
        }
    }

    if (highestRetiredFrameId_ > 0 && frameId <= highestRetiredFrameId_) {
        stats_.staleTokens++;
    }

    if (activeFrames_.size() >= 128) {
        CompleteFrameRecordLocked(activeFrames_.front());
        activeFrames_.pop_front();
    }

    activeFrames_.push_back({frameId, token, 0, 0, 0, 0, false, false, false, false});
    return activeFrames_.back();
}

void StreamlineReflexTracker::CompleteFrameRecordLocked(FrameRecord& record) {
    if (record.completed) return;
    record.completed = true;
    stats_.framesAnalyzed++;

    if (record.sleepCount == 0) {
        stats_.missingSleeps++;
    }

    constexpr std::uint32_t kMandatoryStageMask = 0x1FE;
    const bool hasAllStages = (record.stageMask & kMandatoryStageMask) == kMandatoryStageMask;
    if (!hasAllStages) {
        stats_.missingMarkerFrames++;
    }

    if (record.sleepCount == 1 && !record.duplicateSleep &&
        !record.mixedToken && !record.orderViolation && hasAllStages) {
        stats_.perfectFrames++;
    }

    highestRetiredFrameId_ = std::max(highestRetiredFrameId_, record.frameId);

    while (activeFrames_.size() > 64 && activeFrames_.front().completed) {
        activeFrames_.pop_front();
    }
}

} // namespace nrfusion::streamline
