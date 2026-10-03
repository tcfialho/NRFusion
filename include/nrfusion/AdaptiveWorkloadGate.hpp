#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace nrfusion {

enum class AdaptiveWorkloadState : std::uint32_t {
    Waiting = 0,
    Valid = 1
};

class AdaptiveWorkloadGate {
public:
    static AdaptiveWorkloadGate& Instance() noexcept;

    bool IsSampleValid() const noexcept;
    AdaptiveWorkloadState GetState() const noexcept;

    void RecordFrame(double dtSeconds = 0.0) noexcept;

    void SetTestOverride(int overrideState) noexcept;
    int GetTestOverride() const noexcept;

    void Reset() noexcept;

private:
    AdaptiveWorkloadGate() noexcept = default;
    ~AdaptiveWorkloadGate() noexcept = default;

    AdaptiveWorkloadGate(const AdaptiveWorkloadGate&) = delete;
    AdaptiveWorkloadGate& operator=(const AdaptiveWorkloadGate&) = delete;

    std::atomic<AdaptiveWorkloadState> state_{AdaptiveWorkloadState::Waiting};
    std::atomic<int> testOverride_{-1};
    std::atomic<bool> isTracking_{false};
    std::atomic<std::uint64_t> lastFrameQpc_{0};
    std::atomic<double> accumulatedStableDuration_{0.0};
};

} // namespace nrfusion
