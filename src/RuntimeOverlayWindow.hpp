#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace nrfusion {

class RuntimeOverlay;

class RuntimeOverlayWindow {
public:
    static HWND Create(RuntimeOverlay* owner, HWND parent);
    static void Destroy(HWND hwnd);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static void OnPaint(HWND hwnd, RuntimeOverlay* owner);
    static void OnLButtonDown(HWND hwnd, int x, int y, RuntimeOverlay* owner);
    static void DrawMainTab(HDC hdc, const RECT& rc, RuntimeOverlay* owner);
    static void DrawAdvancedTab(HDC hdc, const RECT& rc, RuntimeOverlay* owner);
    static void DrawHeader(HDC hdc, const RECT& rc, RuntimeOverlay* owner);
};

} // namespace nrfusion
