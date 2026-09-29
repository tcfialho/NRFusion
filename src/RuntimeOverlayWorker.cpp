#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/RuntimeOverlayWorker.hpp"
#include "nrfusion/RuntimeOverlay.hpp"

#include <chrono>

namespace nrfusion {

namespace {

struct WindowSearchContext {
    DWORD targetPid{0};
    HWND bestHwnd{nullptr};
    LONG bestArea{0};
};

} // namespace

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
    const DWORD currentPid = GetCurrentProcessId();
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid == currentPid && IsWindowVisible(fg)) {
            RECT rc{};
            GetClientRect(fg, &rc);
            const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
            if (area > 40000) {
                return fg;
            }
        }
    }

    WindowSearchContext ctx{currentPid, nullptr, 0};
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* c = reinterpret_cast<WindowSearchContext*>(lParam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == c->targetPid && IsWindowVisible(hwnd)) {
            const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
            if (!(exStyle & WS_EX_TOOLWINDOW)) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
                if (area > c->bestArea && area > 40000) {
                    c->bestArea = area;
                    c->bestHwnd = hwnd;
                }
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.bestHwnd;
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
