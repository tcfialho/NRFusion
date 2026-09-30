#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeToast.hpp"
#include "nrfusion/RuntimeLocalization.hpp"
#include "nrfusion/RuntimeConfigStore.hpp"
#include "nrfusion/RuntimeAdvancedConfigStore.hpp"
#include "nrfusion/RuntimeShell.hpp"
#include "RuntimeOverlayWindow.hpp"
#include "nrfusion/GameWindowFinder.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/StreamlineDlssgHook.hpp"
#include "nrfusion/Logger.hpp"

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
    ConfigureFrameGeneration();

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
    if (!activeAdv_.nr.precisionAuto && activeAdv_.nr.precision != NrPrecision::Fp8) {
        activeAdv_.nr.precisionAuto = true;
        activeAdv_.nr.precision = NrPrecision::Fp8;
        NRF_LOG_WARN("Overlay", "Unavailable saved precision; using NVIDIA FP8");
    }

    generation_ = activeMain_.generation;
}

bool RuntimeOverlay::SaveConfigurations() {
    const auto dir = GetModuleDirectory();
    RuntimeConfigStore mainStore(dir / "nrfusion.ini");
    const bool mainSaved = mainStore.Save(activeMain_);
    RuntimeAdvancedConfigStore advStore(dir / "nrfusion_advanced.ini");
    return advStore.Save(activeAdv_) && mainSaved;
}

void RuntimeOverlay::ToggleMenu() {
    if (IsMenuOpen()) {
        CloseMenu();
    } else {
        OpenMenu();
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
    if (!gameWindow_ || !IsWindow(gameWindow_)) {
        gameWindow_ = FindGameWindow();
    }
    RuntimeToast::Instance().Show(message, type, durationMs);
}

void RuntimeOverlay::ShowToast(const std::wstring& message, ToastType type, std::uint32_t durationMs) {
    if (InFrameRendering()) return;
    if (!gameWindow_ || !IsWindow(gameWindow_)) {
        gameWindow_ = FindGameWindow();
    }
    RuntimeToast::Instance().Show(message, type, durationMs);
}

void RuntimeOverlay::EnsureUiWindow() {
    if (!gameWindow_ || !IsWindow(gameWindow_)) {
        gameWindow_ = FindGameWindow();
    }
    if (!uiHwnd_) {
        uiHwnd_ = RuntimeOverlayWindow::Create(this, gameWindow_);
    } else if (gameWindow_) {
        HWND curOwner = GetWindow(uiHwnd_, GW_OWNER);
        if (!curOwner) {
            SetWindowLongPtrW(uiHwnd_, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow_));
        }
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
    status.neuralRuntimeReady = neuralRuntimeReady_.load();
    status.rayReconstruction = rayReconstruction_.load();
    status.beforeUpscale = beforeUpscale_.load();
    status.workingScale = neuralWorkingScale_.load();
    status.nrGpuMs = neuralGpuMs_.load();
    status.passes = neuralPasses_.load();
    if (shell_) {
        const auto shellCfg = shell_->Config();
        const auto lastFrame = lastNeuralFrameTick_.load(std::memory_order_acquire);
        status.nrActive = shellCfg.enabled && lastFrame && GetTickCount64() - lastFrame <= 500;
        status.currentFps = shellCfg.targetFps;
        status.effectiveMultiplier = static_cast<std::uint8_t>(DlssgTransfusion::Instance().Snapshot().effectiveMultiplier);
        status.precision = activeAdv_.nr.precision;
    } else {
        const auto lastFrame = lastNeuralFrameTick_.load(std::memory_order_acquire);
        status.nrActive = activeMain_.enabled && lastFrame && GetTickCount64() - lastFrame <= 500;
        status.currentFps = activeMain_.targetFps;
        status.effectiveMultiplier = static_cast<std::uint8_t>(DlssgTransfusion::Instance().Snapshot().effectiveMultiplier);
    }
    menuDrawing_.UpdateStatus(status);
}

void RuntimeOverlay::ApplyStagedConfiguration() {
    std::unique_lock<std::mutex> lock(mutex_);
    RuntimeConfig proposedMain{};
    RuntimeAdvancedConfig proposedAdv{};

    const std::uint64_t currentGen = shell_ ? shell_->Config().generation : generation_;
    const std::uint64_t nextGen = (currentGen >= generation_ ? currentGen : generation_) + 1;
    if (!menuDrawing_.ProposeCommit(nextGen, &proposedMain, &proposedAdv)) {
        proposedMain = menuDrawing_.MainDraft();
        proposedMain.generation = nextGen;
        proposedAdv = menuDrawing_.AdvancedDraft();
    }

    bool reconfigured = true;
    if (!proposedMain.Valid() || !proposedAdv.Valid() ||
        (!proposedAdv.nr.precisionAuto && proposedAdv.nr.precision != NrPrecision::Fp8)) {
        NRF_LOG_WARN("Overlay", "Configuration rejected: invalid values or unavailable precision");
        configurationResult_.store(ConfigurationResult::Invalid);
        ShowToast(RuntimeLocalization::Strings().toastFailed, ToastType::Error, 3000);
        return;
    }
    if (shell_) {
        reconfigured = shell_->Reconfigure(proposedMain);
    }

    if (reconfigured) {
        generation_ = nextGen;
        menuDrawing_.AcceptCommit(proposedMain, proposedAdv);
        activeMain_ = proposedMain;
        activeAdv_ = proposedAdv;

        ConfigureFrameGeneration();
        const bool saved = SaveConfigurations();
        configurationResult_.store(saved ? ConfigurationResult::AppliedAndSaved : ConfigurationResult::AppliedSaveFailed);
        UpdateTelemetrySnapshot();
        lock.unlock();
        StreamlineDlssgHook::Instance().TriggerLiveMultiplierUpdate();
        ShowToast(saved ? RuntimeLocalization::Strings().toastSaved : RuntimeLocalization::Strings().toastFailed,
                  saved ? ToastType::Success : ToastType::Error, 2500);
    } else {
        configurationResult_.store(ConfigurationResult::Failed);
        ShowToast(RuntimeLocalization::Strings().toastFailed, ToastType::Error, 3000);
    }
}

} // namespace nrfusion
