#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/CaptureD3D11Runtime.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeOverlayWorker.hpp"

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
    nrfusion::StartCaptureD3D11Runtime();
    nrfusion::RuntimeOverlayWorker::Instance().Start(&nrfusion::RuntimeOverlay::Instance());
}

extern "C" __declspec(dllexport) void NRFusion_ShutdownRuntime() {
    if (!g_runtimeStarted.exchange(false)) return;
    nrfusion::RuntimeOverlayWorker::Instance().Stop(false);
    nrfusion::RuntimeOverlay::Instance().Shutdown();
    nrfusion::StopCaptureD3D11Runtime(false);
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

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
