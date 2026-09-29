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
    using RuntimeFn = void(*)();
    auto ensureRuntime = reinterpret_cast<RuntimeFn>(
        GetProcAddress(proxy, "NRFusion_EnsureRuntime"));
    auto shutdownRuntime = reinterpret_cast<RuntimeFn>(
        GetProcAddress(proxy, "NRFusion_ShutdownRuntime"));

    if (!hasVersionForwarder || !hasCaptureRuntime || !ensureRuntime || !shutdownRuntime) return 1;
    ensureRuntime();
    shutdownRuntime();
    return 0;
}
