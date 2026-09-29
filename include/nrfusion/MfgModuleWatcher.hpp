#pragma once

#include <atomic>
#include <thread>
#include <windows.h>

namespace nrfusion {

class MfgModuleWatcher {
public:
    static MfgModuleWatcher& Instance();

    bool Start();
    void Stop(bool isProcessTerminating = false);
    bool IsRunning() const noexcept { return running_.load(); }
    bool ModuleObserved() const noexcept { return observed_.load(); }

private:
    MfgModuleWatcher() = default;
    ~MfgModuleWatcher() { Stop(true); }

    MfgModuleWatcher(const MfgModuleWatcher&) = delete;
    MfgModuleWatcher& operator=(const MfgModuleWatcher&) = delete;

    static void CALLBACK Notification(
        unsigned long reason, const void* data, void* context);
    void QueueLoadedModule(HMODULE module) noexcept;
    void ThreadProc();

    std::atomic<bool> running_{false};    std::atomic<bool> observed_{false};
    std::atomic<HMODULE> pending_{nullptr};
    std::thread worker_;
    HANDLE wakeEvent_{nullptr};
    void* notificationCookie_{nullptr};
};

} // namespace nrfusion
