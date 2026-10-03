#include "StreamlineAutoPerformance.hpp"
#include "nrfusion/AdaptiveWorkloadGate.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion::streamline {

#pragma pack(push, 8)
struct GpuDynamicPstatesInfoEx {
    std::uint32_t version;
    std::uint32_t flags;
    struct {
        std::uint32_t bIsPresent : 1;
        std::uint32_t percentage;
    } utilization[8];
};
#pragma pack(pop)

StreamlineAutoPerformance::StreamlineAutoPerformance() noexcept = default;

StreamlineAutoPerformance& StreamlineAutoPerformance::Instance() noexcept {
    static StreamlineAutoPerformance instance;
    return instance;
}

void StreamlineAutoPerformance::SetEnabled(bool enabled) noexcept {
    enabled_ = enabled;
}

bool StreamlineAutoPerformance::IsEnabled() const noexcept {
    return enabled_;
}

void StreamlineAutoPerformance::Reset(std::uint32_t initialNativeFps) noexcept {
    state_ = AutoMfgState::Armed;
    targetNativeFps_ = initialNativeFps > 0 ? initialNativeFps : 60;
    targetMfgFps_ = targetNativeFps_ * 2;
    lastEvaluationTick_ = 0;
    lastGpuPercent_ = 0;
    discoveryStartTick_ = 0;
    discoverySampleCount_ = 0;
    discoveryFpsSum_ = 0.0f;
}

void StreamlineAutoPerformance::ResetDiscovery() noexcept {
    state_ = AutoMfgState::Armed;
    discoveryStartTick_ = 0;
    discoverySampleCount_ = 0;
    discoveryFpsSum_ = 0.0f;
    lastEvaluationTick_ = 0;
}

void StreamlineAutoPerformance::SetMultiplier(std::uint32_t multiplier) noexcept {
    const uint32_t mult = multiplier > 0 ? multiplier : 2;
    targetMfgFps_ = targetNativeFps_ * mult;
}

AutoMfgState StreamlineAutoPerformance::GetState() const noexcept {
    if (state_ == AutoMfgState::Active || state_ == AutoMfgState::Hold) {
        return AdaptiveWorkloadGate::Instance().IsSampleValid() ? AutoMfgState::Active : AutoMfgState::Hold;
    }
    return state_;
}

bool StreamlineAutoPerformance::IsLimiterActive() const noexcept {
    return state_ == AutoMfgState::Active || state_ == AutoMfgState::Hold;
}

std::uint64_t StreamlineAutoPerformance::GetCurrentTick() const noexcept {
    return tickOverride_ != 0 ? tickOverride_ : GetTickCount64();
}

void StreamlineAutoPerformance::SetTickOverride(std::uint64_t tick) noexcept {
    tickOverride_ = tick;
}

bool StreamlineAutoPerformance::QueryGpuUtilization(std::uint32_t& outGpuPercent) noexcept {
    if (!initializedGpuQuery_) {
        initializedGpuQuery_ = true;
        HMODULE nvapiMod = GetModuleHandleW(L"nvapi64.dll");
        if (!nvapiMod) nvapiMod = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (nvapiMod) {
            auto queryInterface = reinterpret_cast<NvAPI_QueryInterface_t>(
                GetProcAddress(nvapiMod, "nvapi_QueryInterface"));
            if (queryInterface) {
                nvapiInit_ = reinterpret_cast<NvAPI_Initialize_t>(queryInterface(0x0150E828));
                nvapiEnumGpus_ = reinterpret_cast<NvAPI_EnumPhysicalGPUs_t>(queryInterface(0xE5AC921F));
                nvapiGetPstates_ = reinterpret_cast<NvAPI_GPU_GetDynamicPstatesInfoEx_t>(queryInterface(0x60DED2ED));
                if (nvapiInit_ && nvapiEnumGpus_ && nvapiGetPstates_ && nvapiInit_() == 0) {
                    void* handles[64]{};
                    unsigned int gpuCount = 0;
                    if (nvapiEnumGpus_(handles, &gpuCount) == 0 && gpuCount > 0 && handles[0]) {
                        physicalGpuHandle_ = handles[0];
                        gpuQueryAvailable_ = true;
                    }
                }
            }
        }
    }
    if (!gpuQueryAvailable_ || !physicalGpuHandle_ || !nvapiGetPstates_) return false;

    GpuDynamicPstatesInfoEx pstates{};
    pstates.version = sizeof(GpuDynamicPstatesInfoEx) | (1 << 16);
    if (nvapiGetPstates_(physicalGpuHandle_, &pstates) != 0) return false;
    outGpuPercent = pstates.utilization[0].percentage;
    return true;
}

bool StreamlineAutoPerformance::Evaluate(
    std::uint32_t multiplier, float observedNativeFps, AutoPerformanceResult& outResult) noexcept {
    if (!enabled_) return false;
    const bool sampleValid = AdaptiveWorkloadGate::Instance().IsSampleValid();
    const uint32_t mult = multiplier > 0 ? multiplier : 2;
    const uint64_t now = GetCurrentTick();

    if (state_ == AutoMfgState::Armed) {
        if (!sampleValid) return false;
        state_ = AutoMfgState::Discovering;
        discoveryStartTick_ = now;
        discoverySampleCount_ = 0;
        discoveryFpsSum_ = 0.0f;
        return false;
    }

    if (state_ == AutoMfgState::Discovering) {
        if (!sampleValid) {
            state_ = AutoMfgState::Armed;
            discoveryStartTick_ = 0;
            discoverySampleCount_ = 0;
            discoveryFpsSum_ = 0.0f;
            return false;
        }
        if (observedNativeFps > 10.0f) {
            discoveryFpsSum_ += observedNativeFps;
            discoverySampleCount_++;
        }
        if (discoveryStartTick_ != 0 && (now - discoveryStartTick_ >= 2000)) {
            const float avgFps = (discoverySampleCount_ > 0) ? (discoveryFpsSum_ / discoverySampleCount_) : observedNativeFps;
            const float baseline = (avgFps > 10.0f) ? avgFps : (observedNativeFps > 10.0f ? observedNativeFps : 60.0f);
            targetNativeFps_ = std::clamp(static_cast<std::uint32_t>(std::round(baseline * 0.93f)), 24u, 300u);
            targetMfgFps_ = targetNativeFps_ * mult;
            const uint32_t newLimitUs = static_cast<std::uint32_t>(
                std::round(1'000'000.0 / (targetMfgFps_ > 0 ? targetMfgFps_ : 60)));

            state_ = AutoMfgState::Active;
            lastEvaluationTick_ = now;

            outResult.changed = true;
            outResult.targetNativeFps = targetNativeFps_;
            outResult.targetMfgFps = targetMfgFps_;
            outResult.frameLimitUs = newLimitUs;
            outResult.gpuUtilization = lastGpuPercent_;
            return true;
        }
        return false;
    }

    if (!sampleValid) {
        state_ = AutoMfgState::Hold;
        return false;
    }
    state_ = AutoMfgState::Active;

    if (lastEvaluationTick_ != 0 && (now - lastEvaluationTick_ < 2000)) return false;
    lastEvaluationTick_ = now;

    uint32_t gpuPercent = 0;
    const bool haveGpuLoad = QueryGpuUtilization(gpuPercent);
    if (haveGpuLoad) lastGpuPercent_ = gpuPercent;

    const float curTarget = static_cast<float>(targetNativeFps_);
    uint32_t newTarget = targetNativeFps_;

    if (haveGpuLoad) {
        const bool cpuBound = (gpuPercent < 85 && observedNativeFps > 0.0f && observedNativeFps < 0.92f * curTarget);
        if (cpuBound) {
            // CPU bottleneck: keep target stable
        } else if (gpuPercent >= 97 || (observedNativeFps > 0.0f && observedNativeFps < 0.95f * curTarget && gpuPercent >= 85)) {
            newTarget = std::max(24u, static_cast<std::uint32_t>(std::round(curTarget * 0.95f)));
        } else if (gpuPercent <= 90 && observedNativeFps >= 10.0f && observedNativeFps >= 0.98f * curTarget) {
            newTarget = std::min(300u, static_cast<std::uint32_t>(std::round(curTarget * 1.025f)));
        }
    }

    const uint32_t newMfgFps = newTarget * mult;
    const uint32_t newLimitUs = static_cast<std::uint32_t>(
        std::round(1'000'000.0 / (newMfgFps > 0 ? newMfgFps : 60)));

    const bool changed = (newTarget != targetNativeFps_ || newMfgFps != targetMfgFps_);
    targetNativeFps_ = newTarget;
    targetMfgFps_ = newMfgFps;

    outResult.changed = changed;
    outResult.targetNativeFps = targetNativeFps_;
    outResult.targetMfgFps = targetMfgFps_;
    outResult.frameLimitUs = newLimitUs;
    outResult.gpuUtilization = lastGpuPercent_;
    return changed;
}

std::uint32_t StreamlineAutoPerformance::GetCurrentNativeFps() const noexcept {
    return targetNativeFps_;
}

std::uint32_t StreamlineAutoPerformance::GetCurrentMfgFps() const noexcept {
    return targetMfgFps_;
}

std::uint32_t StreamlineAutoPerformance::GetLastGpuUtilization() const noexcept {
    return lastGpuPercent_;
}

} // namespace nrfusion::streamline
