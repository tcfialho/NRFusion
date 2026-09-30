#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/Logger.hpp"
#include "RuntimeOverlayCursor.hpp"
#include "nrfusion/DlssgTransfusion.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace nrfusion {

void RuntimeOverlay::ConfigureFrameGeneration() {
    auto& generation = DlssgTransfusion::Instance();
    generation.SetAutomaticMultiplierLimit(activeAdv_.mfg.allowExperimental56x ? 6u : 4u);
    if (activeMain_.mfgMode == RuntimeMfgMode::Off) {
        generation.SetOverrideMultiplier(1);
        generation.SetControlMode(MfgControlMode::OverrideFixed);
    } else if (activeMain_.mfgMode == RuntimeMfgMode::Fixed) {
        generation.SetOverrideMultiplier(activeMain_.mfgMultiplier);
        generation.SetControlMode(MfgControlMode::OverrideFixed);
    } else if (activeMain_.mfgMode == RuntimeMfgMode::Dynamic) {
        generation.SetDynamicTargetFps(activeMain_.displayHzAuto ? 0 : static_cast<std::uint32_t>(activeMain_.displayHz));
        generation.SetControlMode(MfgControlMode::Dynamic);
    } else {
        generation.SetControlMode(MfgControlMode::FollowGame);
    }
    NRF_LOG_INFO("Overlay", "Configured NR=%d MFG mode=%u multiplier=%u target=%u",
        activeMain_.enabled, static_cast<unsigned>(activeMain_.mfgMode), activeMain_.mfgMultiplier,
        generation.GetDynamicTargetFps());
}

void RuntimeOverlay::OpenMenu() {
    if (!initialized_.load()) return;
    std::lock_guard<std::mutex> lock(mutex_);

    if (shell_) {
        activeMain_ = shell_->Config();
    }
    menuDrawing_.Open(activeMain_, activeAdv_);
    UpdateTelemetrySnapshot();

    if (inFrameRendering_.load(std::memory_order_acquire)) {
        BeginOverlayCursorControl();
        NRF_LOG_INFO("Overlay", "Opened in-frame menu hwnd=%p foreground=%p iconic=%d",
                     gameWindow_, GetForegroundWindow(), gameWindow_ ? IsIconic(gameWindow_) : 0);
        return;
    }

    EnsureUiWindow();
    if (uiHwnd_) {
        SetWindowPos(uiHwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(uiHwnd_);
    }

    cursorShowCount_ = 0;
    int cur = ShowCursor(TRUE);
    if (cur <= 0) {
        cursorShowCount_ = 1;
        while (cur < 0) {
            cur = ShowCursor(TRUE);
            cursorShowCount_++;
        }
    } else {
        ShowCursor(FALSE);
        cursorShowCount_ = 0;
    }
    NRF_LOG_INFO("Overlay", "Opened menu (uiHwnd=%p)", uiHwnd_);
}

void RuntimeOverlay::CloseMenu() {
    std::lock_guard<std::mutex> lock(mutex_);
    menuDrawing_.Close();
    if (uiHwnd_) {
        ShowWindow(uiHwnd_, SW_HIDE);
    }

    for (int i = 0; i < cursorShowCount_; ++i) {
        ShowCursor(FALSE);
    }
    cursorShowCount_ = 0;
    EndOverlayCursorControl();
    NRF_LOG_INFO("Overlay", "Closed menu");
}

void RuntimeOverlay::GetActiveConfiguration(RuntimeConfig& main, RuntimeAdvancedConfig& advanced) {
    std::lock_guard lock(mutex_);
    main = activeMain_;
    advanced = activeAdv_;
}

void RuntimeOverlay::ObserveNeuralFrame(bool applied) noexcept {
    lastNeuralFrameTick_.store(applied ? GetTickCount64() : 0, std::memory_order_release);
}

void RuntimeOverlay::ObserveNeuralRuntime(bool ready, bool rayReconstruction) noexcept {
    neuralRuntimeReady_.store(ready);
    rayReconstruction_.store(rayReconstruction);
}

void RuntimeOverlay::ObserveNeuralWork(float scale, bool beforeUpscale, std::uint32_t passes, double gpuMs) noexcept {
    neuralWorkingScale_.store(scale);
    beforeUpscale_.store(beforeUpscale);
    neuralPasses_.store(passes);
    if (gpuMs > 0) neuralGpuMs_.store(static_cast<float>(gpuMs));
}

void RuntimeOverlay::PollHotkey() {
    if (!initialized_.load()) return;

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
        if (escDown && !hotkeyEscPressed_ && !editingText_.load()) CloseMenu();
        hotkeyEscPressed_ = escDown;
    } else {
        hotkeyEscPressed_ = false;
    }

    if (IsMenuOpen() && !editingText_.load()) {
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
