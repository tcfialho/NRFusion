#pragma once
#include "nrfusion/RuntimeOverlay.hpp"
#include <atomic>
#include <cassert>
#include <thread>

namespace nrfusion {
class RuntimeOverlaySnapshotTestAccess {
public:
    static void Publish(RuntimeOverlay& overlay, const RuntimeConfig& main, const RuntimeAdvancedConfig& advanced) {
        std::lock_guard lock(overlay.mutex_);
        overlay.activeMain_ = main;
        overlay.activeAdv_ = advanced;
        overlay.configurationGeneration_.fetch_add(1, std::memory_order_release);
    }
    static std::mutex& Mutex(RuntimeOverlay& overlay) { return overlay.mutex_; }
};
}

inline void TestRuntimeConfigurationSnapshots() {
    using namespace nrfusion;
    auto& overlay = RuntimeOverlay::Instance();
    overlay.Shutdown();
    RuntimeConfig main{};
    RuntimeAdvancedConfig advanced{};
    std::uint64_t generation = 0;
    assert(overlay.GetActiveConfigurationIfChanged(generation, main, advanced));
    assert(generation != 0);
    {
        std::lock_guard lock(RuntimeOverlaySnapshotTestAccess::Mutex(overlay));
        assert(!overlay.GetActiveConfigurationIfChanged(generation, main, advanced));
    }
    RuntimeConfig requested{};
    RuntimeAdvancedConfig requestedAdvanced{};
    requested.targetFps = 100;
    requested.enabled = true;
    requestedAdvanced.nr.workingScale = 1;
    RuntimeOverlaySnapshotTestAccess::Publish(overlay, requested, requestedAdvanced);
    assert(overlay.GetActiveConfigurationIfChanged(generation, main, advanced));
    assert(main == requested && advanced.nr.workingScale == 1);
    std::atomic<bool> finished{false};
    std::thread writer([&] {
        for (unsigned index = 0; index < 10000; ++index) {
            requested.generation = index + 1;
            requested.targetFps = index % 2 ? 100.0f : 50.0f;
            requested.enabled = requested.targetFps == 100;
            requestedAdvanced.nr.workingScale = requested.targetFps / 100;
            RuntimeOverlaySnapshotTestAccess::Publish(overlay, requested, requestedAdvanced);
        }
        finished.store(true, std::memory_order_release);
    });
    while (!finished.load(std::memory_order_acquire)) {
        if (!overlay.GetActiveConfigurationIfChanged(generation, main, advanced)) continue;
        assert(advanced.nr.workingScale == main.targetFps / 100);
        assert(main.enabled == (main.targetFps == 100));
    }
    writer.join();
    overlay.GetActiveConfigurationIfChanged(generation, main, advanced);
    assert(main.generation == 10000);
    assert(!overlay.GetActiveConfigurationIfChanged(generation, main, advanced));
    overlay.SetInFrameRendering(true);
    overlay.Initialize();
    assert(overlay.GetActiveConfigurationIfChanged(generation, main, advanced));
    const auto firstInitialization = generation;
    overlay.Shutdown();
    overlay.Initialize();
    assert(overlay.GetActiveConfigurationIfChanged(generation, main, advanced));
    assert(generation > firstInitialization);
    overlay.Shutdown();
}
