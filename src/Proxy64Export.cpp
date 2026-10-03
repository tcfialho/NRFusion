#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/CaptureD3D11Runtime.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/MfgModuleWatcher.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeOverlayWorker.hpp"
#include "nrfusion/StreamlineDlssgHook.hpp"
#include "nrfusion/Logger.hpp"
#include "nrfusion/NrKernelProfile.hpp"
#include "NrKernelAbiDiscovery.hpp"

#include <atomic>

namespace {

std::atomic<bool> g_runtimeStarted{false};

void PinProxyModule() noexcept {
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&PinProxyModule), &module);
}

} // namespace

extern "C" __declspec(dllexport) void NRFusion_EnsureRuntime() {
    bool expected = false;
    if (!g_runtimeStarted.compare_exchange_strong(expected, true)) return;
    PinProxyModule();
    nrfusion::Logger::Instance().Initialize(nullptr);
    NRF_LOG_INFO("Proxy", "NRFusion_EnsureRuntime: starting watchers and runtime");
    if (nrfusion::NrKernelProfiler::Instance().StartDriverDiscovery())
        NRF_LOG_INFO("KernelDiscovery", "NVAPI interface observation enabled");
    nrfusion::StreamlineDlssgHook::Instance().Install();
    nrfusion::MfgModuleWatcher::Instance().Start();
    nrfusion::StartCaptureD3D11Runtime();
    nrfusion::RuntimeOverlayWorker::Instance().Start(&nrfusion::RuntimeOverlay::Instance());
}

extern "C" __declspec(dllexport) void NRFusion_ShutdownRuntime() {
    if (!g_runtimeStarted.exchange(false)) return;
    auto& profiler = nrfusion::NrKernelProfiler::Instance();
    profiler.StopDriverDiscovery();
    if (!nrfusion::kernelprofile::FlushAbiDiscovery())
        NRF_LOG_WARN("KernelDiscovery", "Selected kernel ABI metadata capture incomplete");
    const auto report = profiler.Report();
    if (report.frames)
        NRF_LOG_INFO("KernelDiscovery", "%s", profiler.FormatReport().c_str());
    for (const auto& entry : profiler.DriverInterfaces())
        NRF_LOG_INFO("KernelDiscovery", "interface=0x%08x function=0x%llx observations=%llu",
            entry.interfaceId, static_cast<unsigned long long>(entry.functionId),
            static_cast<unsigned long long>(entry.observations));
    nrfusion::RuntimeOverlayWorker::Instance().Stop(false);
    nrfusion::RuntimeOverlay::Instance().Shutdown();
    nrfusion::StopCaptureD3D11Runtime(false);
    nrfusion::MfgModuleWatcher::Instance().Stop(false);
    nrfusion::Logger::Instance().Flush();
}

extern "C" __declspec(dllexport) void NRFusion_SetMenuOpen(int open) {
    NRFusion_EnsureRuntime();
    auto& overlay = nrfusion::RuntimeOverlay::Instance();
    if (open) {
        if (!overlay.IsMenuOpen()) overlay.OpenMenu();
    } else if (overlay.IsMenuOpen()) {
        overlay.CloseMenu();
    }
}

extern "C" __declspec(dllexport) int NRFusion_MenuIsOpen() {
    return nrfusion::RuntimeOverlay::Instance().IsMenuOpen() ? 1 : 0;
}

extern "C" __declspec(dllexport) int NRFusion_MenuIsInFrame() {
    return nrfusion::RuntimeOverlay::Instance().InFrameRendering() ? 1 : 0;
}

extern "C" __declspec(dllexport) int NRFusion_SetNrDiagnosticEnabled(int enabled) {
    NRFusion_EnsureRuntime();
    nrfusion::RuntimeOverlay::Instance().SetDiagnosticEnabledOverride(enabled != 0);
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_MfgModuleObserved() {
    return nrfusion::MfgModuleWatcher::Instance().ModuleObserved() ? 1 : 0;
}

extern "C" __declspec(dllexport) unsigned int NRFusion_MfgUnlockedMax() {
    return nrfusion::DlssgTransfusion::Instance().UnlockedMax();
}

extern "C" __declspec(dllexport) int NRFusion_MfgPatchQualified() {
    const auto state = nrfusion::DlssgTransfusion::Instance().Snapshot();
    const bool active = state.blackwellTransfusionActive ||
                        state.kernelSelector == nrfusion::MfgKernelSelector::StockOnly;
    return (state.archGatesPatched && active) ? 1 : 0;
}

extern "C" __declspec(dllexport) int NRFusion_PatchMfgModule(HMODULE module) {
    if (module) {
        nrfusion::DlssgTransfusion::Instance().TryApply(module);
    } else {
        nrfusion::MfgModuleWatcher::Instance().ScanAndPatchLoadedModules();
    }
    const auto state = nrfusion::DlssgTransfusion::Instance().Snapshot();
    const bool active = state.blackwellTransfusionActive ||
                        state.kernelSelector == nrfusion::MfgKernelSelector::StockOnly;
    return (state.archGatesPatched && active) ? 1 : 0;
}

extern "C" __declspec(dllexport) void NRFusion_MfgFollowGameControl() {
    nrfusion::DlssgTransfusion::Instance().SetControlMode(
        nrfusion::MfgControlMode::FollowGame);
}

extern "C" __declspec(dllexport) int NRFusion_MfgProcessSetOptions(
    unsigned int* mode, unsigned int* framesToGenerate) {
    if (!mode || !framesToGenerate) return 0;
    nrfusion::DlssgTransfusion::Instance().ProcessSetOptions(
        *mode, *framesToGenerate);
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_MfgProcessGetState(
    unsigned int* maximumFramesToGenerate) {
    if (!maximumFramesToGenerate) return 0;
    nrfusion::DlssgTransfusion::Instance().ProcessGetState(
        *maximumFramesToGenerate);
    return 1;
}

extern "C" __declspec(dllexport) void NRFusion_MfgNotifyFrameBoundary() {
    nrfusion::DlssgTransfusion::Instance().NotifyFrameBoundary();
}

extern "C" __declspec(dllexport) int NRFusion_GetReflexOwnership(nrfusion::ReflexOwnershipInfo* outInfo) {
    if (!outInfo) return 0;
    *outInfo = nrfusion::StreamlineDlssgHook::Instance().ReflexOwnership();
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_GetReflexStats(nrfusion::ReflexValidationStats* outStats) {
    if (!outStats) return 0;
    *outStats = nrfusion::StreamlineDlssgHook::Instance().ReflexStats();
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_SetMfgLatencyMode(unsigned int mode, unsigned int targetNativeFps) {
    nrfusion::StreamlineDlssgHook::Instance().SetMfgLatencyMode(
        static_cast<nrfusion::MfgLatencyMode>(mode), targetNativeFps);
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_GetMfgLatencyMode(unsigned int* outMode, unsigned int* outTargetFps) {
    if (outMode) *outMode = static_cast<unsigned int>(nrfusion::StreamlineDlssgHook::Instance().GetMfgLatencyMode());
    if (outTargetFps) *outTargetFps = nrfusion::StreamlineDlssgHook::Instance().GetTargetNativeFps();
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        nrfusion::Logger::Instance().Initialize(hinstDLL);
        NRF_LOG_INFO("Proxy", "NRFusion version.dll attached to PID %lu", GetCurrentProcessId());
        HANDLE hInitThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            NRFusion_EnsureRuntime();
            return 0;
        }, nullptr, 0, nullptr);
        if (hInitThread) {
            CloseHandle(hInitThread);
        }
    }
    return TRUE;
}
