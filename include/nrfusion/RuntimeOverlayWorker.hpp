#pragma once

#include <windows.h>
#include <atomic>
#include <thread>

namespace nrfusion {

class RuntimeOverlay;

class RuntimeOverlayWorker {
public:
    static RuntimeOverlayWorker& Instance();

    void Start(RuntimeOverlay* overlay = nullptr);
    void Stop(bool isProcessTerminating = false);
    bool IsRunning() const { return running_.load(); }

    static HWND FindGameWindow();

private:
    RuntimeOverlayWorker() = default;
    ~RuntimeOverlayWorker() { Stop(false); }

    RuntimeOverlayWorker(const RuntimeOverlayWorker&) = delete;
    RuntimeOverlayWorker& operator=(const RuntimeOverlayWorker&) = delete;

    void ThreadProc();

    std::atomic<bool> running_{false};
    std::thread workerThread_;
    RuntimeOverlay* overlay_{nullptr};
    DWORD workerThreadId_{0};
};

} // namespace nrfusion
