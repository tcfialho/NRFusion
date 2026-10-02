#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi1_6.h>

int main() {
    if (GetModuleHandleW(L"d3d12.dll")) return 6;
    HMODULE proxy = LoadLibraryW(L"nrfusion_proxy.dll");
    if (!proxy) return 1;
    if (GetModuleHandleW(L"d3d12.dll")) return 7;

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
        GetProcAddress(proxy, "NRFusion_ReadNrDiagnosticFrame") != nullptr &&
        GetProcAddress(proxy, "NRFusion_SetNrDiagnosticEnabled") != nullptr;
    const bool hasMenuDiagnostics =
        GetProcAddress(proxy, "NRFusion_SetMenuOpen") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MenuIsOpen") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MenuIsInFrame") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MenuGpuDrawCount") != nullptr;
    const bool hasMfgDiagnostics =
        GetProcAddress(proxy, "NRFusion_MfgModuleObserved") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgUnlockedMax") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgPatchQualified") != nullptr &&
        GetProcAddress(proxy, "NRFusion_PatchMfgModule") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgFollowGameControl") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgProcessSetOptions") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgProcessGetState") != nullptr &&
        GetProcAddress(proxy, "NRFusion_MfgNotifyFrameBoundary") != nullptr;
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
    using FactoryFunction = HRESULT(WINAPI*)(REFIID, void**);
    const auto createFactory = reinterpret_cast<FactoryFunction>(GetProcAddress(proxy, "CreateDXGIFactory1"));
    if (!createFactory) return 2;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) return 3;
    factory->Release();
    using Factory2Function = HRESULT(WINAPI*)(UINT, REFIID, void**);
    const auto createFactory2 = reinterpret_cast<Factory2Function>(GetProcAddress(proxy, "CreateDXGIFactory2"));
    if (!createFactory2) return 4;
    IDXGIFactory2* factory2 = nullptr;
    if (FAILED(createFactory2(0, __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2))) || !factory2) return 5;
    factory2->Release();
    shutdownRuntime();
    return 0;
}
