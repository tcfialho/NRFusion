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

    return hasVersionForwarder && hasCaptureRuntime ? 0 : 1;
}
