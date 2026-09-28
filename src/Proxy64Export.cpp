#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/CaptureD3D11Runtime.hpp"

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        nrfusion::StartCaptureD3D11Runtime();
        break;
    case DLL_PROCESS_DETACH:
        nrfusion::StopCaptureD3D11Runtime(lpvReserved != nullptr);
        break;
    }
    return TRUE;
}
