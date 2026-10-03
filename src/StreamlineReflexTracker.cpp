#include "StreamlineReflexTracker.hpp"
#include "StreamlineAutoPerformance.hpp"
#include "nrfusion/AdaptiveWorkloadGate.hpp"
#include "nrfusion/RuntimeConfigStore.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion::streamline {

StreamlineReflexTracker::StreamlineReflexTracker() noexcept {
    RuntimeConfigStore store("nrfusion.ini"); RuntimeConfig cfg{};
    if (store.Load(1, cfg)) {
        latencyMode_ = cfg.mfgLatencyMode; isAutoPerformance_ = cfg.mfgTargetAuto;
        targetDisplayFps_ = cfg.mfgTargetAuto ? 0 : cfg.mfgTargetFps; pacerMode_ = cfg.mfgPacer;
    }
    wchar_t buf[64]{};
    if (GetEnvironmentVariableW(L"NRFUSION_MFG_LATENCY_MODE", buf, 64) > 0) {
        if (_wcsicmp(buf, L"GameDefault") == 0 || wcscmp(buf, L"0") == 0) latencyMode_ = MfgLatencyMode::GameDefault;
        else if (_wcsicmp(buf, L"LowLatency") == 0 || wcscmp(buf, L"1") == 0) latencyMode_ = MfgLatencyMode::LowLatency;
    }
    if (GetEnvironmentVariableW(L"NRFUSION_MFG_TARGET_DISPLAY_FPS", buf, 32) > 0) {
        if (_wcsicmp(buf, L"Auto") == 0 || _wcsicmp(buf, L"AutoPerformance") == 0 || wcscmp(buf, L"0") == 0) {
            isAutoPerformance_ = true; targetDisplayFps_ = 0;
        } else {
            const auto fps = wcstoul(buf, nullptr, 10);
            if (fps >= 20 && fps <= 1000) { targetDisplayFps_ = static_cast<std::uint32_t>(fps); isAutoPerformance_ = false; }
        }
    } else if (GetEnvironmentVariableW(L"NRFUSION_MFG_TARGET_FPS", buf, 32) > 0) {
        const auto fps = wcstoul(buf, nullptr, 10);
        if (fps >= 20 && fps <= 1000) { targetDisplayFps_ = static_cast<std::uint32_t>(fps); isAutoPerformance_ = false; }
    }
    StreamlineAutoPerformance::Instance().SetEnabled(isAutoPerformance_);
    if (GetEnvironmentVariableW(L"NRFUSION_MFG_PACER", buf, 32) > 0) {
        if (_wcsicmp(buf, L"CpuPacer") == 0 || wcscmp(buf, L"1") == 0) pacerMode_ = MfgPacerMode::CpuPacer;
        else if (_wcsicmp(buf, L"FlipMetering") == 0 || wcscmp(buf, L"2") == 0) pacerMode_ = MfgPacerMode::FlipMetering;
        else pacerMode_ = MfgPacerMode::Auto;
    }
}

StreamlineReflexTracker& StreamlineReflexTracker::Instance() noexcept {
    static StreamlineReflexTracker instance;
    return instance;
}

sl::ReflexOptions StreamlineReflexTracker::CalculateEffectiveOptionsLocked(bool mfgActive) const {
    sl::ReflexOptions applied = lastGameRequested_;
    if (mfgActive) {
        if (lastGameRequested_.mode == sl::ReflexMode::eOff) applied.mode = sl::ReflexMode::eLowLatency;
        if (latencyMode_ == MfgLatencyMode::LowLatency) {
            uint32_t effectiveDisplayFps = targetDisplayFps_;
            if (isAutoPerformance_) {
                if (StreamlineAutoPerformance::Instance().IsLimiterActive()) {
                    const auto autoFps = StreamlineAutoPerformance::Instance().GetCurrentMfgFps();
                    if (autoFps > 0) effectiveDisplayFps = autoFps;
                    if (effectiveDisplayFps == 0) effectiveDisplayFps = 60;
                    applied.frameLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / effectiveDisplayFps));
                }
            } else {
                if (effectiveDisplayFps == 0) effectiveDisplayFps = 60;
                applied.frameLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / effectiveDisplayFps));
            }
        }
    }
    return applied;
}

sl::ReflexOptions StreamlineReflexTracker::OnGameReflexSetOptions(const sl::ReflexOptions& requested, bool mfgActive) {
    std::lock_guard lock(mutex_);
    lastGameRequested_ = requested; haveGameRequested_ = true;
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
        outOptions = target; mfgPromotedReflex_ = (mfgActive && lastGameRequested_.mode == sl::ReflexMode::eOff);
        lastEffective_ = target; return true;
    }
    return false;
}

void StreamlineReflexTracker::SetLatencyMode(MfgLatencyMode mode, std::uint32_t targetDisplayFps, std::uint32_t targetNativeFps) noexcept {
    std::lock_guard lock(mutex_);
    latencyMode_ = mode;
    if (targetDisplayFps > 0) {
        targetDisplayFps_ = targetDisplayFps; isAutoPerformance_ = false;
        StreamlineAutoPerformance::Instance().SetEnabled(false);
        wchar_t numBuf[16]{}; swprintf_s(numBuf, L"%u", targetDisplayFps);
        SetEnvironmentVariableW(L"NRFUSION_MFG_TARGET_DISPLAY_FPS", numBuf);
    }
    if (targetNativeFps > 0) targetNativeFps_ = targetNativeFps;
}

void StreamlineReflexTracker::SetAutoPerformance(bool enabled) noexcept {
    std::lock_guard lock(mutex_);
    const bool wasAuto = isAutoPerformance_;
    isAutoPerformance_ = enabled;
    StreamlineAutoPerformance::Instance().SetEnabled(enabled);
    if (enabled) {
        if (!wasAuto) StreamlineAutoPerformance::Instance().ResetDiscovery();
        SetEnvironmentVariableW(L"NRFUSION_MFG_TARGET_DISPLAY_FPS", L"Auto");
    } else if (targetDisplayFps_ > 0) {
        wchar_t numBuf[16]{}; swprintf_s(numBuf, L"%u", targetDisplayFps_);
        SetEnvironmentVariableW(L"NRFUSION_MFG_TARGET_DISPLAY_FPS", numBuf);
    }
}

bool StreamlineReflexTracker::IsAutoPerformance() const noexcept { std::lock_guard lock(mutex_); return isAutoPerformance_; }
std::uint32_t StreamlineReflexTracker::GetAutoState() const noexcept { std::lock_guard lock(mutex_); return static_cast<std::uint32_t>(StreamlineAutoPerformance::Instance().GetState()); }
void StreamlineReflexTracker::ResetDiscovery() noexcept { std::lock_guard lock(mutex_); StreamlineAutoPerformance::Instance().ResetDiscovery(); }
MfgLatencyMode StreamlineReflexTracker::GetLatencyMode() const noexcept { std::lock_guard lock(mutex_); return latencyMode_; }

std::uint32_t StreamlineReflexTracker::GetTargetDisplayFps() const noexcept {
    std::lock_guard lock(mutex_);
    return isAutoPerformance_ ? StreamlineAutoPerformance::Instance().GetCurrentMfgFps() : targetDisplayFps_;
}

std::uint32_t StreamlineReflexTracker::GetTargetNativeFps() const noexcept {
    std::lock_guard lock(mutex_);
    if (isAutoPerformance_) return StreamlineAutoPerformance::Instance().GetCurrentNativeFps();
    const uint32_t mult = mfgMultiplier_ > 0 ? mfgMultiplier_ : 2;
    return (targetDisplayFps_ > 0 ? targetDisplayFps_ : 60) / mult;
}

void StreamlineReflexTracker::SetMfgMultiplier(std::uint32_t multiplier) noexcept {
    std::lock_guard lock(mutex_);
    mfgMultiplier_ = multiplier > 0 ? multiplier : 2;
    StreamlineAutoPerformance::Instance().SetMultiplier(mfgMultiplier_);
    if (latencyMode_ == MfgLatencyMode::LowLatency && haveGameRequested_) {
        uint32_t effectiveDisplayFps = targetDisplayFps_;
        if (isAutoPerformance_) {
            if (StreamlineAutoPerformance::Instance().IsLimiterActive()) {
                const auto autoFps = StreamlineAutoPerformance::Instance().GetCurrentMfgFps();
                if (autoFps > 0) effectiveDisplayFps = autoFps;
                if (effectiveDisplayFps == 0) effectiveDisplayFps = 60;
                lastEffective_.frameLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / effectiveDisplayFps));
            }
        } else {
            if (effectiveDisplayFps == 0) effectiveDisplayFps = 60;
            lastEffective_.frameLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / effectiveDisplayFps));
        }
    }
}

bool StreamlineReflexTracker::ApplyCurrentLatencyPolicy(bool mfgActive, sl::ReflexOptions& outOptions) {
    std::lock_guard lock(mutex_);
    if (!haveGameRequested_) return false;
    sl::ReflexOptions target = CalculateEffectiveOptionsLocked(mfgActive);
    if (target.mode != lastEffective_.mode || target.frameLimitUs != lastEffective_.frameLimitUs) {
        outOptions = target; mfgPromotedReflex_ = (mfgActive && lastGameRequested_.mode == sl::ReflexMode::eOff);
        lastEffective_ = target; return true;
    }
    return false;
}

void StreamlineReflexTracker::RecordSleep(const sl::FrameToken& frame) {
    std::lock_guard lock(mutex_);
    const auto fid = static_cast<std::uint32_t>(frame);
    auto& rec = GetOrCreateFrameRecordLocked(fid, &frame);
    rec.sleepCount++;
    if (rec.sleepCount > 1) { rec.duplicateSleep = true; stats_.duplicateSleeps++; }
    else { rec.firstSleepTick = GetTickCount64(); }
    constexpr auto sleepStage = static_cast<std::uint32_t>(FrameStage::Sleep);
    if (rec.lastStage > sleepStage) { rec.orderViolation = true; stats_.orderViolations++; }
    rec.lastStage = std::max(rec.lastStage, sleepStage); rec.stageMask |= (1u << sleepStage);

    LARGE_INTEGER qpc{}, freq{};
    QueryPerformanceCounter(&qpc); QueryPerformanceFrequency(&freq);
    if (lastFrameQpc_ > 0 && freq.QuadPart > 0) {
        const double dt = static_cast<double>(qpc.QuadPart - lastFrameQpc_) / freq.QuadPart;
        if (dt > 0.001 && dt < 0.5) {
            const float sampleFps = static_cast<float>(1.0 / dt);
            measuredNativeFps_ = (measuredNativeFps_ > 0.0f) ? (measuredNativeFps_ + 0.10f * (sampleFps - measuredNativeFps_)) : sampleFps;
        }
        AdaptiveWorkloadGate::Instance().RecordFrame(dt);
    } else { AdaptiveWorkloadGate::Instance().RecordFrame(0.0); }
    lastFrameQpc_ = static_cast<std::uint64_t>(qpc.QuadPart);
    if (isAutoPerformance_ && latencyMode_ == MfgLatencyMode::LowLatency) {
        AutoPerformanceResult res{};
        StreamlineAutoPerformance::Instance().Evaluate(mfgMultiplier_, measuredNativeFps_, res);
    }
}

void StreamlineReflexTracker::RecordMarker(sl::PCLMarker marker, const sl::FrameToken& frame) {
    std::lock_guard lock(mutex_);
    FrameStage stage = FrameStage::None;
    const auto raw = static_cast<std::uint32_t>(marker);
    if (raw == 6 || marker == sl::PCLMarker::eControllerInputSample) stage = FrameStage::InputSample;
    else if (marker == sl::PCLMarker::eSimulationStart) stage = FrameStage::SimulationStart;
    else if (marker == sl::PCLMarker::eSimulationEnd) stage = FrameStage::SimulationEnd;
    else if (marker == sl::PCLMarker::eRenderSubmitStart) stage = FrameStage::RenderSubmitStart;
    else if (marker == sl::PCLMarker::eRenderSubmitEnd) stage = FrameStage::RenderSubmitEnd;
    else if (marker == sl::PCLMarker::ePresentStart) stage = FrameStage::PresentStart;
    else if (marker == sl::PCLMarker::ePresentEnd) stage = FrameStage::PresentEnd;
    if (stage == FrameStage::None) return;

    auto& rec = GetOrCreateFrameRecordLocked(static_cast<std::uint32_t>(frame), &frame);
    const auto currentStage = static_cast<std::uint32_t>(stage);
    const bool isStart = (stage == FrameStage::InputSample || stage == FrameStage::SimulationStart);
    const bool wasStart = (rec.lastStage == static_cast<std::uint32_t>(FrameStage::Sleep) ||
                           rec.lastStage == static_cast<std::uint32_t>(FrameStage::SimulationStart) ||
                           rec.lastStage == static_cast<std::uint32_t>(FrameStage::InputSample));
    if (!(isStart && wasStart) && currentStage < rec.lastStage) { rec.orderViolation = true; stats_.orderViolations++; }
    rec.lastStage = std::max(rec.lastStage, currentStage); rec.stageMask |= (1u << currentStage);
    if (stage == FrameStage::PresentEnd) CompleteFrameRecordLocked(rec);
}

ReflexOwnershipInfo StreamlineReflexTracker::GetOwnershipInfo() const noexcept {
    std::lock_guard lock(mutex_);
    ReflexOwnershipInfo info{};
    info.haveGameOptions = haveGameRequested_;
    info.gameMode = static_cast<std::uint32_t>(lastGameRequested_.mode);
    info.gameFrameLimitUs = lastGameRequested_.frameLimitUs;
    info.gameUseMarkersToOptimize = lastGameRequested_.useMarkersToOptimize;
    info.gameVirtualKey = lastGameRequested_.virtualKey; info.gameIdThread = lastGameRequested_.idThread;
    info.effectiveMode = static_cast<std::uint32_t>(lastEffective_.mode);
    info.effectiveFrameLimitUs = lastEffective_.frameLimitUs;
    info.effectiveUseMarkersToOptimize = lastEffective_.useMarkersToOptimize;
    info.effectiveVirtualKey = lastEffective_.virtualKey; info.effectiveIdThread = lastEffective_.idThread;
    info.mfgPromotedReflex = mfgPromotedReflex_; info.latencyMode = static_cast<std::uint32_t>(latencyMode_);
    info.autoPerformance = isAutoPerformance_;
    info.autoState = static_cast<std::uint32_t>(StreamlineAutoPerformance::Instance().GetState());

    const uint32_t mult = mfgMultiplier_ > 0 ? mfgMultiplier_ : 2;
    if (latencyMode_ == MfgLatencyMode::LowLatency) {
        if (isAutoPerformance_) {
            info.targetDisplayFps = StreamlineAutoPerformance::Instance().GetCurrentMfgFps();
            info.targetNativeFps = StreamlineAutoPerformance::Instance().GetCurrentNativeFps();
        } else {
            const uint32_t dispFps = targetDisplayFps_ > 0 ? targetDisplayFps_ : 60;
            info.targetDisplayFps = dispFps; info.targetNativeFps = dispFps / mult;
        }
    }
    return info;
}

void StreamlineReflexTracker::SetPacerMode(MfgPacerMode mode) noexcept {
    std::lock_guard lock(mutex_);
    pacerMode_ = mode;
    SetEnvironmentVariableW(L"NRFUSION_MFG_PACER", mode == MfgPacerMode::CpuPacer ? L"CpuPacer" :
        (mode == MfgPacerMode::FlipMetering ? L"FlipMetering" : L"Auto"));
}

MfgPacerMode StreamlineReflexTracker::GetPacerMode() const noexcept { std::lock_guard lock(mutex_); return pacerMode_; }
ReflexValidationStats StreamlineReflexTracker::GetValidationStats() const noexcept { std::lock_guard lock(mutex_); return stats_; }

void StreamlineReflexTracker::Reset() noexcept {
    std::lock_guard lock(mutex_);
    lastGameRequested_ = {}; lastEffective_ = {};
    haveGameRequested_ = false; mfgPromotedReflex_ = false;
    activeFrames_.clear(); highestRetiredFrameId_ = 0; stats_ = {};
}

StreamlineReflexTracker::FrameRecord& StreamlineReflexTracker::GetOrCreateFrameRecordLocked(std::uint32_t frameId, const void* token) {
    for (auto& rec : activeFrames_) {
        if (rec.frameId == frameId) {
            if (rec.tokenPtr && token && rec.tokenPtr != token && !rec.mixedToken) { rec.mixedToken = true; stats_.mixedTokens++; }
            return rec;
        }
    }
    if (highestRetiredFrameId_ > 0 && frameId <= highestRetiredFrameId_) stats_.staleTokens++;
    if (activeFrames_.size() >= 128) { CompleteFrameRecordLocked(activeFrames_.front()); activeFrames_.pop_front(); }
    activeFrames_.push_back({frameId, token, 0, 0, 0, 0, false, false, false, false});
    return activeFrames_.back();
}

void StreamlineReflexTracker::CompleteFrameRecordLocked(FrameRecord& record) {
    if (record.completed) return;
    record.completed = true; stats_.framesAnalyzed++;
    if (record.sleepCount == 0) stats_.missingSleeps++;
    constexpr std::uint32_t kMandatoryStageMask = 0x1FE;
    if ((record.stageMask & kMandatoryStageMask) != kMandatoryStageMask) stats_.missingMarkerFrames++;
    if (record.sleepCount == 1 && !record.duplicateSleep && !record.mixedToken && !record.orderViolation &&
        (record.stageMask & kMandatoryStageMask) == kMandatoryStageMask) stats_.perfectFrames++;
    highestRetiredFrameId_ = std::max(highestRetiredFrameId_, record.frameId);
    while (activeFrames_.size() > 64 && activeFrames_.front().completed) activeFrames_.pop_front();
}

} // namespace nrfusion::streamline
