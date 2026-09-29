#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int main() {
    HMODULE proxy = LoadLibraryW(L"nrfusion_proxy.dll");
    if (!proxy) return 1;

    const bool hasVersionForwarder =
        GetProcAddress(proxy, "GetFileVersionInfoA") != nullptr;
    const bool hasCaptureRuntime =
        GetProcAddress(proxy, "NRFusion_Capture32_Connect") != nullptr;
    const bool hasNgxD3D12 =
        GetProcAddress(proxy, "NVSDK_NGX_D3D12_Init") != nullptr &&
        GetProcAddress(proxy, "NVSDK_NGX_D3D12_CreateFeature") != nullptr &&
        GetProcAddress(proxy, "NVSDK_NGX_D3D12_EvaluateFeature") != nullptr &&
        GetProcAddress(proxy, "NVSDK_NGX_D3D12_ReleaseFeature") != nullptr;
    const bool hasNrDiagnostics =
        GetProcAddress(proxy, "NRFusion_BeginNrDiagnosticFrame") != nullptr &&
        GetProcAddress(proxy, "NRFusion_ReadNrDiagnosticFrame") != nullptr;
    const bool hasMenuDiagnostics =
        GetProcAddress(proxy, "NRFusion_SetMenuOpen") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MenuIsOpen") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MenuIsInFrame") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MenuGpuDrawCount") != nullptr;
    const bool hasMfgDiagnostics =
        GetProcAddress(proxy, "NRFusion_MfgModuleObserved") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgUnlockedMax") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgPatchQualified") != nullptr;
    using RuntimeFn = void(*)();
    auto ensureRuntime = reinterpret_cast<RuntimeFn>(
        GetProcAddress(proxy, "NRFusion_EnsureRuntime"));
    auto shutdownRuntime = reinterpret_cast<RuntimeFn>(
        GetProcAddress(proxy, "NRFusion_ShutdownRuntime"));

    if (!hasVersionForwarder || !hasCaptureRuntime || !hasNgxD3D12 ||
        !hasNrDiagnostics || !hasMenuDiagnostics ||
        !hasMfgDiagnostics || !ensureRuntime || !shutdownRuntime)
        return 1;
    ensureRuntime();
    shutdownRuntime();
    return 0;
}
