#include "nrfusion/RuntimeOverlay.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace nrfusion {

void RuntimeOverlay::PollHotkey() {
    if (!initialized_.load()) return;
    if (IsMenuOpen() && !InFrameRendering()) ClipCursor(nullptr);

    const HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fgPid = 0;
        GetWindowThreadProcessId(fg, &fgPid);
        if (fgPid != GetCurrentProcessId()) return;
    }

    const bool f8Down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8Down && !hotkeyF8Pressed_) ToggleMenu();
    hotkeyF8Pressed_ = f8Down;
    const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (insertDown && !hotkeyInsertPressed_) ToggleMenu();
    hotkeyInsertPressed_ = insertDown;

    if (IsMenuOpen()) {
        const bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        if (escDown && !hotkeyEscPressed_) CloseMenu();
        hotkeyEscPressed_ = escDown;
    } else {
        hotkeyEscPressed_ = false;
    }

    if (IsMenuOpen() && InFrameRendering()) {
        auto draft = menuDrawing_.MainDraft();
        const bool enabledDown = (GetAsyncKeyState('E') & 0x8000) != 0;
        if (enabledDown && !hotkeyEnabledPressed_) {
            draft.enabled = !draft.enabled;
            menuDrawing_.StageMain(draft);
        }
        hotkeyEnabledPressed_ = enabledDown;

        const bool leftDown = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
        if (leftDown && !hotkeyLeftPressed_) {
            const int mode = (static_cast<int>(draft.mode) + 3) % 4;
            draft.mode = static_cast<RuntimeNrMode>(mode);
            menuDrawing_.StageMain(draft);
        }
        hotkeyLeftPressed_ = leftDown;

        const bool rightDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
        if (rightDown && !hotkeyRightPressed_) {
            const int mode = (static_cast<int>(draft.mode) + 1) % 4;
            draft.mode = static_cast<RuntimeNrMode>(mode);
            menuDrawing_.StageMain(draft);
        }
        hotkeyRightPressed_ = rightDown;

        const bool upDown = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        if (upDown && !hotkeyUpPressed_) {
            draft.targetFps = (draft.targetFps < 240.0f)
                ? draft.targetFps + 5.0f : 240.0f;
            menuDrawing_.StageMain(draft);
        }
        hotkeyUpPressed_ = upDown;
        const bool downDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        if (downDown && !hotkeyDownPressed_) {
            draft.targetFps = (draft.targetFps > 30.0f)
                ? draft.targetFps - 5.0f : 30.0f;
            menuDrawing_.StageMain(draft);
        }
        hotkeyDownPressed_ = downDown;

        const bool applyDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        if (applyDown && !hotkeyApplyPressed_) ApplyStagedConfiguration();
        hotkeyApplyPressed_ = applyDown;
        UpdateTelemetrySnapshot();
    } else {
        hotkeyApplyPressed_ = false;
        hotkeyEnabledPressed_ = false;
        hotkeyLeftPressed_ = false;
        hotkeyRightPressed_ = false;
        hotkeyUpPressed_ = false;
        hotkeyDownPressed_ = false;
    }

    if (IsMenuOpen() && uiHwnd_) {
        UpdateTelemetrySnapshot();
        InvalidateRect(uiHwnd_, nullptr, FALSE);
    }
}

} // namespace nrfusion
