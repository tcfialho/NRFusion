#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>

namespace nrfusion::streamline {

struct AutoPerformanceResult {
    bool changed = false;
    std::uint32_t targetNativeFps = 0;
    std::uint32_t targetMfgFps = 0;
    std::uint32_t frameLimitUs = 0;
    std::uint32_t gpuUtilization = 0;
};

class StreamlineAutoPerformance {
public:
    static StreamlineAutoPerformance& Instance() noexcept;

    void SetEnabled(bool enabled) noexcept;
    bool IsEnabled() const noexcept;

    void Reset(std::uint32_t initialNativeFps = 0) noexcept;
    void SetMultiplier(std::uint32_t multiplier) noexcept;

    bool Evaluate(std::uint32_t multiplier, float observedNativeFps, AutoPerformanceResult& outResult) noexcept;

    std::uint32_t GetCurrentNativeFps() const noexcept;
    std::uint32_t GetCurrentMfgFps() const noexcept;
    std::uint32_t GetLastGpuUtilization() const noexcept;

private:
    StreamlineAutoPerformance() noexcept;
    ~StreamlineAutoPerformance() noexcept = default;

    bool QueryGpuUtilization(std::uint32_t& outGpuPercent) noexcept;

    bool enabled_ = true;
    std::uint32_t targetNativeFps_ = 60;
    std::uint32_t targetMfgFps_ = 120;
    std::uint32_t lastGpuPercent_ = 0;
    std::uint64_t lastEvaluationTick_ = 0;
    bool initializedGpuQuery_ = false;

    using NvAPI_QueryInterface_t = void*(*)(unsigned int);
    using NvAPI_Initialize_t = int(*)();
    using NvAPI_EnumPhysicalGPUs_t = int(*)(void**, unsigned int*);
    using NvAPI_GPU_GetDynamicPstatesInfoEx_t = int(*)(void*, void*);

    NvAPI_Initialize_t nvapiInit_ = nullptr;
    NvAPI_EnumPhysicalGPUs_t nvapiEnumGpus_ = nullptr;
    NvAPI_GPU_GetDynamicPstatesInfoEx_t nvapiGetPstates_ = nullptr;
    void* physicalGpuHandle_ = nullptr;
    bool gpuQueryAvailable_ = false;
};

} // namespace nrfusion::streamline
