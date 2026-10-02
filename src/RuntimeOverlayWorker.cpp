#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/RuntimeOverlayWorker.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/GameWindowFinder.hpp"
#include "nrfusion/Logger.hpp"

#include <chrono>

namespace nrfusion {

RuntimeOverlayWorker& RuntimeOverlayWorker::Instance() {
    static RuntimeOverlayWorker instance;
    return instance;
}

void RuntimeOverlayWorker::Start(RuntimeOverlay* overlay) {
    if (running_.exchange(true)) return;
    overlay_ = overlay ? overlay : &RuntimeOverlay::Instance();
    workerThread_ = std::thread(&RuntimeOverlayWorker::ThreadProc, this);
}

void RuntimeOverlayWorker::Stop(bool isProcessTerminating) {
    if (!running_.exchange(false)) return;
    if (isProcessTerminating) {
        if (workerThread_.joinable()) {
            workerThread_.detach();
        }
        return;
    }
    if (workerThreadId_ != 0) {
        PostThreadMessageW(workerThreadId_, WM_QUIT, 0, 0);
    }
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    workerThreadId_ = 0;
}

HWND RuntimeOverlayWorker::FindGameWindow() {
    return nrfusion::FindGameWindow();
}

void RuntimeOverlayWorker::ThreadProc() {
    workerThreadId_ = GetCurrentThreadId();

    MSG msgInit{};
    PeekMessageW(&msgInit, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HWND gameWnd = nullptr;
    for (int i = 0; i < 30 && running_.load() && !gameWnd; ++i) {
        gameWnd = FindGameWindow();
        if (!gameWnd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (overlay_) {
        overlay_->Initialize(gameWnd);
    }

    while (running_.load()) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running_.store(false);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running_.load()) break;

        if (overlay_) {
            if (!gameWnd) {
                gameWnd = FindGameWindow();
                if (gameWnd) {
                    overlay_->Initialize(gameWnd);
                }
            }
            overlay_->PollHotkey();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace nrfusion
