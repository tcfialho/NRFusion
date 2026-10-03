#include "StreamlineReflexTracker.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion::streamline {

StreamlineReflexTracker::StreamlineReflexTracker() noexcept {
    wchar_t modeBuf[64]{};
    if (GetEnvironmentVariableW(L"NRFUSION_MFG_LATENCY_MODE", modeBuf, 64) > 0) {
        if (_wcsicmp(modeBuf, L"LowLatency") == 0 || wcscmp(modeBuf, L"1") == 0) {
            latencyMode_ = MfgLatencyMode::LowLatency;
        } else if (_wcsicmp(modeBuf, L"OptiScalerEquivalent") == 0 || wcscmp(modeBuf, L"2") == 0) {
            latencyMode_ = MfgLatencyMode::OptiScalerEquivalent;
        } else if (_wcsicmp(modeBuf, L"GameDefault") == 0 || wcscmp(modeBuf, L"0") == 0) {
            latencyMode_ = MfgLatencyMode::GameDefault;
        }
    }
    wchar_t fpsBuf[32]{};
    if (GetEnvironmentVariableW(L"NRFUSION_MFG_TARGET_FPS", fpsBuf, 32) > 0) {
        const unsigned long fps = wcstoul(fpsBuf, nullptr, 10);
        if (fps >= 20 && fps <= 500) targetNativeFps_ = static_cast<std::uint32_t>(fps);
    }
}

StreamlineReflexTracker& StreamlineReflexTracker::Instance() noexcept {
    static StreamlineReflexTracker instance;
    return instance;
}

sl::ReflexOptions StreamlineReflexTracker::CalculateEffectiveOptionsLocked(bool mfgActive) const {
    sl::ReflexOptions applied = lastGameRequested_;
    if (mfgActive) {
        if (lastGameRequested_.mode == sl::ReflexMode::eOff) {
            applied.mode = sl::ReflexMode::eLowLatency;
        } else {
            applied.mode = lastGameRequested_.mode;
        }

        if (latencyMode_ == MfgLatencyMode::LowLatency) {
            const uint32_t target = targetNativeFps_ > 0 ? targetNativeFps_ : 71;
            applied.frameLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / target));
        } else if (latencyMode_ == MfgLatencyMode::OptiScalerEquivalent) {
            const uint32_t refresh = targetNativeFps_ > 0 ? targetNativeFps_ : 144;
            const uint32_t mult = mfgMultiplier_ > 0 ? mfgMultiplier_ : 2;
            const uint32_t nativeTarget = refresh / mult;
            applied.frameLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / (nativeTarget > 0 ? nativeTarget : 1)));
        } else {
            applied.frameLimitUs = lastGameRequested_.frameLimitUs;
        }
    } else {
        applied.mode = lastGameRequested_.mode;
        applied.frameLimitUs = lastGameRequested_.frameLimitUs;
    }
    return applied;
}

sl::ReflexOptions StreamlineReflexTracker::OnGameReflexSetOptions(
    const sl::ReflexOptions& requested, bool mfgActive) {
    std::lock_guard lock(mutex_);
    lastGameRequested_ = requested;
    haveGameRequested_ = true;

    sl::ReflexOptions applied = CalculateEffectiveOptionsLocked(mfgActive);
    mfgPromotedReflex_ = (mfgActive && lastGameRequested_.mode == sl::ReflexMode::eOff);
    lastEffective_ = applied;
    return applied;
}

bool StreamlineReflexTracker::OnMfgStateChanged(bool mfgActive, sl::ReflexOptions& outOptions) {
    std::lock_guard lock(mutex_);
    if (!haveGameRequested_) return false;

    sl::ReflexOptions target = CalculateEffectiveOptionsLocked(mfgActive);
    if (target.mode != lastEffective_.mode || target.frameLimitUs != lastEffective_.frameLimitUs) {
        outOptions = target;
        mfgPromotedReflex_ = (mfgActive && lastGameRequested_.mode == sl::ReflexMode::eOff);
        lastEffective_ = target;
        return true;
    }
    return false;
}

void StreamlineReflexTracker::SetLatencyMode(MfgLatencyMode mode, std::uint32_t targetNativeFps) noexcept {
    std::lock_guard lock(mutex_);
    latencyMode_ = mode;
    if (targetNativeFps > 0) targetNativeFps_ = targetNativeFps;
}

MfgLatencyMode StreamlineReflexTracker::GetLatencyMode() const noexcept {
    std::lock_guard lock(mutex_);
    return latencyMode_;
}

std::uint32_t StreamlineReflexTracker::GetTargetNativeFps() const noexcept {
    std::lock_guard lock(mutex_);
    return targetNativeFps_;
}

void StreamlineReflexTracker::SetMfgMultiplier(std::uint32_t multiplier) noexcept {
    std::lock_guard lock(mutex_);
    mfgMultiplier_ = multiplier > 0 ? multiplier : 2;
}

bool StreamlineReflexTracker::ApplyCurrentLatencyPolicy(bool mfgActive, sl::ReflexOptions& outOptions) {
    std::lock_guard lock(mutex_);
    if (!haveGameRequested_) return false;
    sl::ReflexOptions target = CalculateEffectiveOptionsLocked(mfgActive);
    if (target.mode != lastEffective_.mode || target.frameLimitUs != lastEffective_.frameLimitUs) {
        outOptions = target;
        mfgPromotedReflex_ = (mfgActive && lastGameRequested_.mode == sl::ReflexMode::eOff);
        lastEffective_ = target;
        return true;
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

    const bool isStartPhase = (stage == FrameStage::InputSample || stage == FrameStage::SimulationStart);
    const bool wasStartPhase = (rec.lastStage == static_cast<std::uint32_t>(FrameStage::Sleep) ||
                                rec.lastStage == static_cast<std::uint32_t>(FrameStage::SimulationStart) ||
                                rec.lastStage == static_cast<std::uint32_t>(FrameStage::InputSample));

    if (isStartPhase && wasStartPhase) {
        // Both orders (SimulationStart <-> InputSample) are valid start phase markers
    } else if (currentStage < rec.lastStage) {
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
    info.latencyMode = static_cast<std::uint32_t>(latencyMode_);
    if (latencyMode_ == MfgLatencyMode::LowLatency) {
        info.targetNativeFps = (targetNativeFps_ > 0) ? targetNativeFps_ : 71;
    } else if (latencyMode_ == MfgLatencyMode::OptiScalerEquivalent) {
        const uint32_t refresh = targetNativeFps_ > 0 ? targetNativeFps_ : 144;
        info.targetNativeFps = refresh / (mfgMultiplier_ > 0 ? mfgMultiplier_ : 2);
    } else {
        info.targetNativeFps = 0;
    }
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
