#include "nrfusion/GameWindowFinder.hpp"
#include "nrfusion/Logger.hpp"

#include <cwchar>

namespace nrfusion {

namespace {

struct FindWindowContext {
    DWORD pid{0};
    HWND bestHwnd{nullptr};
    LONG maxArea{0};
};

bool IsValidGameWindowCandidate(HWND hwnd, DWORD targetPid) noexcept {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;

    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid != targetPid) return false;

    // Must be unowned top-level window
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;

    // Exclude our own classes
    wchar_t clsName[256] = {};
    if (GetClassNameW(hwnd, clsName, 256) > 0) {
        if (std::wcscmp(clsName, L"NRFusionOverlayClass") == 0 ||
            std::wcscmp(clsName, L"NRFusionToastClass") == 0) {
            return false;
        }
    }

    // Must have reasonable size
    RECT rc = {};
    if (!GetClientRect(hwnd, &rc)) return false;
    const LONG width = rc.right - rc.left;
    const LONG height = rc.bottom - rc.top;
    if (width < 200 || height < 200) return false;

    return true;
}

BOOL CALLBACK EnumGameWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<FindWindowContext*>(lParam);
    if (!IsValidGameWindowCandidate(hwnd, ctx->pid)) return TRUE;

    RECT rc = {};
    GetClientRect(hwnd, &rc);
    const LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > ctx->maxArea) {
        ctx->maxArea = area;
        ctx->bestHwnd = hwnd;
    }
    return TRUE;
}

} // namespace

HWND FindGameWindow(DWORD targetPid) noexcept {
    const DWORD pid = targetPid ? targetPid : GetCurrentProcessId();

    // 1. Try foreground window first
    HWND fg = GetForegroundWindow();
    if (IsValidGameWindowCandidate(fg, pid)) {
        return fg;
    }

    // 2. Enumerate top-level windows of target PID
    FindWindowContext ctx{};
    ctx.pid = pid;
    EnumWindows(EnumGameWindowsCallback, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.bestHwnd) {
        wchar_t title[256] = {};
        GetWindowTextW(ctx.bestHwnd, title, 256);
        NRF_LOG_INFO("WindowFinder", "Discovered game window HWND=%p, title='%ls', area=%ld",
                     ctx.bestHwnd, title, ctx.maxArea);
    }
    return ctx.bestHwnd;
}

} // namespace nrfusion
