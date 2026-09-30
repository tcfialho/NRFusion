#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <atomic>

namespace nrfusion {

struct StreamlineMfgStatus {
    bool linked = false;
    bool nativeDynamicSupported = false;
    bool updatePending = false;
    std::uint32_t optionsVersion = 0;
    std::uint32_t maxGeneratedFrames = 0;
    std::uint32_t lastResult = 0;
    std::uint32_t runtimeStatus = 0;
    bool dynamicActive = false;
    std::uint32_t framesPresentedInSample = 0;
};

class StreamlineDlssgHook {
public:
    static StreamlineDlssgHook& Instance() noexcept;

    bool Install(HMODULE interposerModule = nullptr) noexcept;
    void TriggerLiveMultiplierUpdate() noexcept;
    StreamlineMfgStatus Status() const noexcept;

    bool IsInstalled() const noexcept { return installed_.load(); }

private:
    StreamlineDlssgHook() noexcept = default;
    ~StreamlineDlssgHook() noexcept = default;

    StreamlineDlssgHook(const StreamlineDlssgHook&) = delete;
    StreamlineDlssgHook& operator=(const StreamlineDlssgHook&) = delete;

    std::atomic<bool> installed_{false};
};

} // namespace nrfusion
