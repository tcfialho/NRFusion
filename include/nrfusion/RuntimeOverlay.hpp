#pragma once

#include "nrfusion/RuntimeMenuDrawing.hpp"
#include "nrfusion/RuntimeToast.hpp"
#include "nrfusion/RuntimeConfigStore.hpp"
#include "nrfusion/RuntimeAdvancedConfigStore.hpp"
#include <mutex>
#include <atomic>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nrfusion {

class RuntimeShell;

enum class ConfigurationResult : std::uint8_t { None, AppliedAndSaved, Invalid, AppliedSaveFailed, Failed };

class RuntimeOverlay {
public:
    static RuntimeOverlay& Instance();

    void Initialize(HWND gameWindow = nullptr, RuntimeShell* shell = nullptr);
    void Shutdown();

    void SetShell(RuntimeShell* shell) noexcept;
    RuntimeShell* GetShell() const noexcept;

    void ToggleMenu();
    void OpenMenu();
    void CloseMenu();
    bool IsMenuOpen() const noexcept;
    void SetInFrameRendering(bool enabled) noexcept;
    bool InFrameRendering() const noexcept;

    void ShowToast(const std::string& message, ToastType type, std::uint32_t durationMs = 3000);
    void ShowToast(const std::wstring& message, ToastType type, std::uint32_t durationMs = 3000);
    void PollHotkey();

    RuntimeMenuDrawing& MenuDrawing() noexcept;
    void ApplyStagedConfiguration();
    void GetActiveConfiguration(RuntimeConfig& main, RuntimeAdvancedConfig& advanced);
    bool GetActiveConfigurationIfChanged(std::uint64_t& cachedGeneration,
        RuntimeConfig& main, RuntimeAdvancedConfig& advanced);
    void ObserveNeuralFrame(bool applied) noexcept;
    void ObserveNeuralRuntime(bool ready, bool rayReconstruction) noexcept;
    void ObserveNeuralWork(float scale, bool beforeUpscale, std::uint32_t passes, double gpuMs) noexcept;
    ConfigurationResult LastConfigurationResult() const noexcept { return configurationResult_.load(); }
    void SetEditingText(bool editing) noexcept { editingText_.store(editing); }

private:
    RuntimeOverlay();
    ~RuntimeOverlay();

    void EnsureUiWindow();
    void DestroyUiWindow();
    void LoadConfigurations();
    bool SaveConfigurations();
    void UpdateTelemetrySnapshot();
    void ConfigureFrameGeneration();

    std::mutex mutex_;
    std::atomic<bool> initialized_{false};
    HWND gameWindow_ = nullptr;
    HWND uiHwnd_ = nullptr;
    RuntimeMenuDrawing menuDrawing_;
    RuntimeConfig activeMain_{};
    RuntimeAdvancedConfig activeAdv_{};
    std::uint64_t generation_ = 1;
    std::atomic<std::uint64_t> configurationGeneration_{1};
    bool hotkeyF8Pressed_ = false;
    bool hotkeyInsertPressed_ = false;
    bool hotkeyEscPressed_ = false;
    bool hotkeyApplyPressed_ = false;
    bool hotkeyEnabledPressed_ = false;
    bool hotkeyLeftPressed_ = false;
    bool hotkeyRightPressed_ = false;
    bool hotkeyUpPressed_ = false;
    bool hotkeyDownPressed_ = false;
    std::atomic<bool> inFrameRendering_{false};
    std::atomic<std::uint64_t> lastNeuralFrameTick_{0};
    std::atomic<bool> neuralRuntimeReady_{false};
    std::atomic<bool> rayReconstruction_{false};
    std::atomic<bool> beforeUpscale_{false};
    std::atomic<float> neuralWorkingScale_{1.0f};
    std::atomic<float> neuralGpuMs_{0.0f};
    std::atomic<std::uint32_t> neuralPasses_{1};
    std::atomic<ConfigurationResult> configurationResult_{ConfigurationResult::None};
    std::atomic<bool> editingText_{false};
    RuntimeShell* shell_ = nullptr;
    std::unique_ptr<RuntimeShell> ownedShell_;

    RECT savedClipRect_{};
    bool hasSavedClip_{false};
    int cursorShowCount_{0};

    friend class RuntimeOverlayWindow;
    friend class RuntimeOverlaySnapshotTestAccess;
};

} // namespace nrfusion
