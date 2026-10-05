#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nrfusion {

class RuntimeOverlay;

class RuntimeOverlayWindow {
public:
    static HWND Create(RuntimeOverlay* owner, HWND parent);
    static void Destroy(HWND hwnd);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

} // namespace nrfusion
