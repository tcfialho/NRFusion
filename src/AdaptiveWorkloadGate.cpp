#include "nrfusion/AdaptiveWorkloadGate.hpp"

namespace nrfusion {

AdaptiveWorkloadGate& AdaptiveWorkloadGate::Instance() noexcept {
    static AdaptiveWorkloadGate instance;
    return instance;
}

bool AdaptiveWorkloadGate::IsSampleValid() const noexcept {
    const int overrideVal = testOverride_.load(std::memory_order_relaxed);
    if (overrideVal == 0) return false;
    if (overrideVal == 1) return true;

    if (!isTracking_.load(std::memory_order_acquire)) return true;

    if (lastFrameQpc_.load(std::memory_order_relaxed) > 0) {
        LARGE_INTEGER qpc{}, freq{};
        QueryPerformanceCounter(&qpc);
        QueryPerformanceFrequency(&freq);
        if (freq.QuadPart > 0) {
            const double elapsed = static_cast<double>(
                qpc.QuadPart - lastFrameQpc_.load(std::memory_order_relaxed)) / freq.QuadPart;
            if (elapsed > 0.150) return false;
        }
    }

    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != 0 && pid != GetCurrentProcessId()) return false;
    }

    return state_.load(std::memory_order_acquire) == AdaptiveWorkloadState::Valid;
}

AdaptiveWorkloadState AdaptiveWorkloadGate::GetState() const noexcept {
    return IsSampleValid() ? AdaptiveWorkloadState::Valid : AdaptiveWorkloadState::Waiting;
}

void AdaptiveWorkloadGate::RecordFrame(double dtSeconds) noexcept {
    isTracking_.store(true, std::memory_order_release);
    LARGE_INTEGER qpc{}, freq{};
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);

    double dt = dtSeconds;
    const auto prevQpc = lastFrameQpc_.exchange(
        static_cast<std::uint64_t>(qpc.QuadPart), std::memory_order_acq_rel);

    if (dt <= 0.0 && prevQpc > 0 && freq.QuadPart > 0) {
        dt = static_cast<double>(qpc.QuadPart - prevQpc) / freq.QuadPart;
    }

    if (dt <= 0.0) return;

    if (dt > 0.150) {
        accumulatedStableDuration_.store(0.0, std::memory_order_release);
        state_.store(AdaptiveWorkloadState::Waiting, std::memory_order_release);
    } else if (dt >= 0.001) {
        double cur = accumulatedStableDuration_.load(std::memory_order_relaxed);
        cur += dt;
        accumulatedStableDuration_.store(cur, std::memory_order_relaxed);
        if (cur >= 1.0) {
            state_.store(AdaptiveWorkloadState::Valid, std::memory_order_release);
        }
    }
}

void AdaptiveWorkloadGate::SetTestOverride(int overrideState) noexcept {
    testOverride_.store(overrideState, std::memory_order_release);
}

int AdaptiveWorkloadGate::GetTestOverride() const noexcept {
    return testOverride_.load(std::memory_order_relaxed);
}

void AdaptiveWorkloadGate::Reset() noexcept {
    state_.store(AdaptiveWorkloadState::Waiting, std::memory_order_release);
    testOverride_.store(-1, std::memory_order_release);
    isTracking_.store(false, std::memory_order_release);
    lastFrameQpc_.store(0, std::memory_order_release);
    accumulatedStableDuration_.store(0.0, std::memory_order_release);
}

} // namespace nrfusion
