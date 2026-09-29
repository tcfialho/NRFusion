#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeToast.hpp"
#include "nrfusion/RuntimeLocalization.hpp"
#include "nrfusion/RuntimeConfigStore.hpp"
#include "nrfusion/RuntimeAdvancedConfigStore.hpp"
#include "nrfusion/RuntimeShell.hpp"
#include "RuntimeOverlayWindow.hpp"

#include <filesystem>
#include <vector>

namespace nrfusion {

namespace {

std::filesystem::path GetModuleDirectory() {
    wchar_t path[MAX_PATH] = {0};
    HMODULE hModule = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetModuleDirectory),
        &hModule);
    if (GetModuleFileNameW(hModule, path, MAX_PATH) > 0) {
        return std::filesystem::path(path).parent_path();
    }
    return std::filesystem::current_path();
}

} // namespace

RuntimeOverlay& RuntimeOverlay::Instance() {
    static RuntimeOverlay s_instance;
    return s_instance;
}

RuntimeOverlay::RuntimeOverlay() = default;

RuntimeOverlay::~RuntimeOverlay() {
    Shutdown();
}

void RuntimeOverlay::Initialize(HWND gameWindow, RuntimeShell* shell) {
    if (initialized_.exchange(true)) {
        if (gameWindow && !gameWindow_) {
            gameWindow_ = gameWindow;
            RuntimeToast::Instance().Initialize(gameWindow_);
        }
        if (shell && !shell_) shell_ = shell;
        return;
    }

    gameWindow_ = gameWindow;
    if (shell) {
        shell_ = shell;
    } else {
        ownedShell_ = std::make_unique<RuntimeShell>();
        shell_ = ownedShell_.get();
    }

    RuntimeToast::Instance().Initialize(gameWindow_);
    LoadConfigurations();

    if (shell_) {
        shell_->Initialize(activeMain_);
    }

    UpdateTelemetrySnapshot();
    ShowToast(RuntimeLocalization::Strings().toastWelcome, ToastType::Success, 3500);
}

void RuntimeOverlay::Shutdown() {
    if (!initialized_.exchange(false)) return;

    CloseMenu();
    DestroyUiWindow();
    RuntimeToast::Instance().Shutdown();
    if (ownedShell_) {
        ownedShell_->Shutdown();
        ownedShell_.reset();
    }
    shell_ = nullptr;
    gameWindow_ = nullptr;
}

void RuntimeOverlay::SetShell(RuntimeShell* shell) noexcept {
    shell_ = shell;
    if (shell_ && initialized_) {
        UpdateTelemetrySnapshot();
    }
}

RuntimeShell* RuntimeOverlay::GetShell() const noexcept {
    return shell_;
}

void RuntimeOverlay::LoadConfigurations() {
    const auto dir = GetModuleDirectory();
    const auto mainIni = dir / "nrfusion.ini";
    const auto advIni = dir / "nrfusion_advanced.ini";

    RuntimeConfigStore mainStore(mainIni);
    if (!mainStore.Load(generation_, activeMain_)) {
        activeMain_.generation = 1;
        activeMain_.enabled = true;
        activeMain_.mode = RuntimeNrMode::Auto;
        activeMain_.targetFps = 90.0f;
        activeMain_.displayHz = 165.0f;
        activeMain_.mfgMode = RuntimeMfgMode::Dynamic;
    }

    RuntimeAdvancedConfigStore advStore(advIni);
    if (!advStore.Load(activeAdv_)) {
        activeAdv_ = RuntimeAdvancedConfig{};
    }

    generation_ = activeMain_.generation;
}

void RuntimeOverlay::SaveConfigurations() {
    const auto dir = GetModuleDirectory();
    RuntimeConfigStore mainStore(dir / "nrfusion.ini");
    mainStore.Save(activeMain_);
    RuntimeAdvancedConfigStore advStore(dir / "nrfusion_advanced.ini");
    advStore.Save(activeAdv_);
}

void RuntimeOverlay::ToggleMenu() {
    if (IsMenuOpen()) {
        CloseMenu();
    } else {
        OpenMenu();
    }
}

void RuntimeOverlay::OpenMenu() {
    if (!initialized_.load()) return;
    std::lock_guard<std::mutex> lock(mutex_);

    if (shell_) {
        activeMain_ = shell_->Config();
    }
    menuDrawing_.Open(activeMain_, activeAdv_);
    UpdateTelemetrySnapshot();

    if (inFrameRendering_.load(std::memory_order_acquire)) return;

    EnsureUiWindow();
    if (uiHwnd_) {
        SetWindowPos(uiHwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(uiHwnd_);
    }

    if (GetClipCursor(&savedClipRect_)) {
        hasSavedClip_ = true;
    }
    ClipCursor(nullptr);
    ReleaseCapture();

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

    if (hasSavedClip_) {
        ClipCursor(&savedClipRect_);
        hasSavedClip_ = false;
    }
}

bool RuntimeOverlay::IsMenuOpen() const noexcept {
    return menuDrawing_.IsOpen();
}

void RuntimeOverlay::SetInFrameRendering(bool enabled) noexcept {
    inFrameRendering_.store(enabled, std::memory_order_release);
    if (enabled) DestroyUiWindow();
}

bool RuntimeOverlay::InFrameRendering() const noexcept {
    return inFrameRendering_.load(std::memory_order_acquire);
}

void RuntimeOverlay::ShowToast(const std::string& message, ToastType type, std::uint32_t durationMs) {
    if (InFrameRendering()) return;
    RuntimeToast::Instance().Show(message, type, durationMs);
}

void RuntimeOverlay::ShowToast(const std::wstring& message, ToastType type, std::uint32_t durationMs) {
    if (InFrameRendering()) return;
    RuntimeToast::Instance().Show(message, type, durationMs);
}

void RuntimeOverlay::EnsureUiWindow() {
    if (!uiHwnd_) {
        uiHwnd_ = RuntimeOverlayWindow::Create(this, gameWindow_);
    }
}

void RuntimeOverlay::DestroyUiWindow() {
    if (uiHwnd_) {
        RuntimeOverlayWindow::Destroy(uiHwnd_);
        uiHwnd_ = nullptr;
    }
}

RuntimeMenuDrawing& RuntimeOverlay::MenuDrawing() noexcept {
    return menuDrawing_;
}

void RuntimeOverlay::UpdateTelemetrySnapshot() {
    MenuStatusSnapshot status{};
    if (shell_) {
        const auto shellStatus = shell_->Status();
        const auto shellCfg = shell_->Config();
        status.nrActive = (shellStatus.state == RuntimeState::Running) && shellCfg.enabled;
        status.currentFps = shellCfg.targetFps;
        status.effectiveMultiplier = static_cast<std::uint32_t>(shellCfg.mfgMultiplier);
        status.precision = activeAdv_.nr.precision;
    } else {
        status.nrActive = activeMain_.enabled;
        status.currentFps = activeMain_.targetFps;
        status.effectiveMultiplier = 1;
    }
    menuDrawing_.UpdateStatus(status);
}

void RuntimeOverlay::ApplyStagedConfiguration() {
    std::lock_guard<std::mutex> lock(mutex_);
    RuntimeConfig proposedMain{};
    RuntimeAdvancedConfig proposedAdv{};

    const std::uint64_t currentGen = shell_ ? shell_->Config().generation : generation_;
    const std::uint64_t nextGen = (currentGen >= generation_ ? currentGen : generation_) + 1;
    generation_ = nextGen;
    if (!menuDrawing_.ProposeCommit(nextGen, &proposedMain, &proposedAdv)) {
        proposedMain = menuDrawing_.MainDraft();
        proposedMain.generation = nextGen;
        proposedAdv = menuDrawing_.AdvancedDraft();
    }

    bool reconfigured = true;
    if (shell_) {
        reconfigured = shell_->Reconfigure(proposedMain);
    }

    if (reconfigured) {
        menuDrawing_.AcceptCommit(proposedMain, proposedAdv);
        activeMain_ = proposedMain;
        activeAdv_ = proposedAdv;
        SaveConfigurations();
        UpdateTelemetrySnapshot();
        ShowToast(RuntimeLocalization::Strings().toastSaved, ToastType::Success, 2500);
    } else {
        ShowToast(RuntimeLocalization::Strings().toastFailed, ToastType::Error, 3000);
    }
}

} // namespace nrfusion
