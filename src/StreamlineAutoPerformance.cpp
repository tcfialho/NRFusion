#include "StreamlineAutoPerformance.hpp"
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
    targetNativeFps_ = initialNativeFps > 0 ? initialNativeFps : 60;
    targetMfgFps_ = targetNativeFps_ * 2;
    lastEvaluationTick_ = 0;
    lastGpuPercent_ = 0;
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
    const uint64_t now = GetTickCount64();
    if (lastEvaluationTick_ != 0 && (now - lastEvaluationTick_ < 2000)) return false;
    lastEvaluationTick_ = now;

    uint32_t gpuPercent = 0;
    const bool haveGpuLoad = QueryGpuUtilization(gpuPercent);
    if (haveGpuLoad) lastGpuPercent_ = gpuPercent;

    const uint32_t mult = multiplier > 0 ? multiplier : 2;

    if (observedNativeFps > 20.0f && (targetNativeFps_ == 0 || targetNativeFps_ == 60)) {
        targetNativeFps_ = std::clamp(static_cast<std::uint32_t>(std::round(observedNativeFps * 0.95f)), 24u, 300u);
    }

    const float curTarget = static_cast<float>(targetNativeFps_);
    uint32_t newTarget = targetNativeFps_;

    if (haveGpuLoad) {
        const bool cpuBound = (gpuPercent < 85 && observedNativeFps > 0.0f && observedNativeFps < 0.92f * curTarget);
        if (cpuBound) {
            // CPU bottleneck: keep target stable, do not aggressively lower
        } else if (gpuPercent >= 97 || (observedNativeFps > 0.0f && observedNativeFps < 0.95f * curTarget && gpuPercent >= 85)) {
            // Fast drop on saturation (~5%)
            newTarget = std::max(24u, static_cast<std::uint32_t>(std::round(curTarget * 0.95f)));
        } else if (gpuPercent <= 90 && observedNativeFps >= 10.0f && observedNativeFps >= 0.98f * curTarget) {
            // Slow rise on headroom (~2.5%)
            newTarget = std::min(300u, static_cast<std::uint32_t>(std::round(curTarget * 1.025f)));
        }
    }

    const uint32_t newMfgFps = newTarget * mult;
    const uint32_t newLimitUs = static_cast<std::uint32_t>(std::round(1'000'000.0 / (newMfgFps > 0 ? newMfgFps : 60)));

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
