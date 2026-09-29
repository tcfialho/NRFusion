#include "RuntimeOverlayWindow.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeLocalization.hpp"
#include <commctrl.h>
#include <sstream>

namespace nrfusion {

namespace {
constexpr const wchar_t* kOverlayClassName = L"NRFusion_Overlay_Menu";
constexpr int kMenuWidth = 460;
constexpr int kMenuHeight = 560;

void DrawTextSimple(HDC hdc, const std::wstring& text, int x, int y, COLORREF color, int size = 15, bool bold = false) {
    HFONT hFont = CreateFontW(
        size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    auto oldFont = SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text.c_str(), static_cast<int>(text.length()));
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}

void DrawButtonBox(HDC hdc, const RECT& rc, const std::wstring& label, bool active) {
    HBRUSH bg = CreateSolidBrush(active ? RGB(40, 44, 55) : RGB(25, 27, 33));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, active ? RGB(80, 140, 220) : RGB(50, 52, 62));
    auto oldPen = SelectObject(hdc, pen);
    auto oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);

    RECT trc = rc;
    HFONT hFont = CreateFontW(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    auto oldFont = SelectObject(hdc, hFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, active ? RGB(255, 255, 255) : RGB(200, 200, 210));
    DrawTextW(hdc, label.c_str(), -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}
} // namespace

HWND RuntimeOverlayWindow::Create(RuntimeOverlay* owner, HWND parent) {
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassExW(&wc);

    int x = 60;
    int y = 60;
    if (parent && IsWindow(parent)) {
        RECT prc;
        GetWindowRect(parent, &prc);
        x = prc.left + 50;
        y = prc.top + 50;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName, L"NRFusion Menu",
        WS_POPUP | WS_BORDER,
        x, y, kMenuWidth, kMenuHeight,
        nullptr, nullptr, hInst, owner
    );
    return hwnd;
}

void RuntimeOverlayWindow::Destroy(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK RuntimeOverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    auto* owner = reinterpret_cast<RuntimeOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        return TRUE;
    case WM_PAINT:
        if (owner) OnPaint(hwnd, owner);
        return 0;
    case WM_LBUTTONDOWN:
        if (owner) OnLButtonDown(hwnd, LOWORD(lParam), HIWORD(lParam), owner);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && owner) {
            owner->CloseMenu();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RuntimeOverlayWindow::OnPaint(HWND hwnd, RuntimeOverlay* owner) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    auto oldBitmap = SelectObject(memDC, memBitmap);

    HBRUSH bgBrush = CreateSolidBrush(RGB(15, 16, 20));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    DrawHeader(memDC, rc, owner);

    RECT contentRc = { rc.left, 85, rc.right, rc.bottom };
    if (owner->MenuDrawing().ActiveTab() == MenuTab::Main) {
        DrawMainTab(memDC, contentRc, owner);
    } else {
        DrawAdvancedTab(memDC, contentRc, owner);
    }

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

void RuntimeOverlayWindow::DrawHeader(HDC hdc, const RECT& rc, RuntimeOverlay* owner) {
    const auto& s = RuntimeLocalization::Strings();
    DrawTextSimple(hdc, L"NRFUSION", 20, 16, RGB(255, 255, 255), 18, true);
    DrawTextSimple(hdc, s.subtitle, 120, 19, RGB(130, 135, 150), 13, false);
    DrawButtonBox(hdc, RECT{ rc.right - 85, 14, rc.right - 45, 34 }, s.langButton, false);
    DrawButtonBox(hdc, RECT{ rc.right - 35, 14, rc.right - 15, 34 }, L"X", false);
    bool isMain = (owner->MenuDrawing().ActiveTab() == MenuTab::Main);
    DrawButtonBox(hdc, RECT{ 20, 50, 140, 78 }, s.tabMain, isMain);
    DrawButtonBox(hdc, RECT{ 150, 50, 270, 78 }, s.tabAdvanced, !isMain);
    HPEN linePen = CreatePen(PS_SOLID, 1, RGB(40, 42, 50));
    auto oldPen = SelectObject(hdc, linePen);
    MoveToEx(hdc, 0, 84, nullptr);
    LineTo(hdc, rc.right, 84);
    SelectObject(hdc, oldPen);
    DeleteObject(linePen);
}

void RuntimeOverlayWindow::DrawMainTab(HDC hdc, const RECT& rc, RuntimeOverlay* owner) {
    const auto& s = RuntimeLocalization::Strings();
    auto draft = owner->MenuDrawing().MainDraft();
    auto status = owner->MenuDrawing().StatusSnapshot();
    int y = rc.top + 10;
    DrawButtonBox(hdc, RECT{ 20, y, 220, y + 28 }, draft.enabled ? s.enabledOn : s.enabledOff, draft.enabled);
    y += 40;
    DrawTextSimple(hdc, s.nrModeTitle, 20, y, RGB(180, 185, 200), 14, true);
    y += 24;
    const wchar_t* modeNames[] = { s.nrModeAuto, s.nrModeQuality, s.nrModePerf, s.nrModeCustom };
    for (int i = 0; i < 4; ++i) {
        DrawButtonBox(hdc, RECT{ 20 + i * 105, y, 120 + i * 105, y + 26 }, modeNames[i], static_cast<int>(draft.mode) == i);
    }
    y += 42;
    std::wstring fpsStr = s.targetFpsPrefix + std::to_wstring(static_cast<int>(draft.targetFps)) + L" FPS";
    DrawTextSimple(hdc, fpsStr, 20, y, RGB(180, 185, 200), 14, true);
    DrawButtonBox(hdc, RECT{ 240, y - 2, 280, y + 24 }, L"-5", false);
    DrawButtonBox(hdc, RECT{ 290, y - 2, 330, y + 24 }, L"+5", false);
    y += 40;
    std::wstring hzStr = s.displayHzPrefix + std::to_wstring(static_cast<int>(draft.displayHz)) + L" Hz";
    DrawTextSimple(hdc, hzStr, 20, y, RGB(180, 185, 200), 14, true);
    y += 40;
    DrawTextSimple(hdc, s.mfgModeTitle, 20, y, RGB(180, 185, 200), 14, true);
    y += 24;
    const wchar_t* mfgNames[] = { s.mfgFollowGame, s.mfgFixed, s.mfgDynamic };
    for (int i = 0; i < 3; ++i) {
        DrawButtonBox(hdc, RECT{ 20 + i * 140, y, 150 + i * 140, y + 26 }, mfgNames[i], static_cast<int>(draft.mfgMode) == i);
    }
    y += 44;
    RECT stBox = { 20, y, 440, y + 70 };
    HBRUSH stBrush = CreateSolidBrush(RGB(20, 22, 28));
    FillRect(hdc, &stBox, stBrush);
    DeleteObject(stBrush);
    DrawTextSimple(hdc, s.statusTitle, 30, y + 8, RGB(120, 160, 240), 13, true);
    DrawTextSimple(hdc, status.nrActive ? s.statusNrActive : s.statusNrInactive, 30, y + 34, RGB(220, 220, 225), 13, false);
    DrawTextSimple(hdc, L"MFG: " + std::to_wstring(status.effectiveMultiplier) + L"X", 160, y + 34, RGB(220, 220, 225), 13, false);
    DrawTextSimple(hdc, s.statusOutput + std::to_wstring(static_cast<int>(status.currentFps)) + L" FPS", 280, y + 34, RGB(220, 220, 225), 13, false);
    DrawButtonBox(hdc, RECT{ 110, rc.bottom - 50, 350, rc.bottom - 15 }, s.btnApply, true);
}

void RuntimeOverlayWindow::DrawAdvancedTab(HDC hdc, const RECT& rc, RuntimeOverlay* owner) {
    const auto& s = RuntimeLocalization::Strings();
    auto adv = owner->MenuDrawing().AdvancedDraft();
    int y = rc.top + 10;
    DrawTextSimple(hdc, s.neuralPrecision, 20, y, RGB(180, 185, 200), 14, true);
    y += 24;
    const wchar_t* precNames[] = { L"Auto", L"FP8", L"FP16", L"W4A8" };
    for (int i = 0; i < 4; ++i) {
        bool sel = (i == 0 && adv.nr.precisionAuto) || (!adv.nr.precisionAuto && static_cast<int>(adv.nr.precision) == (i - 1));
        DrawButtonBox(hdc, RECT{ 20 + i * 105, y, 120 + i * 105, y + 26 }, precNames[i], sel);
    }
    y += 44;
    DrawTextSimple(hdc, s.pipelinePlacement, 20, y, RGB(180, 185, 200), 14, true);
    y += 24;
    const wchar_t* plNames[] = { L"Auto", L"Pre-SR", L"Across RR", L"Post-SR" };
    for (int i = 0; i < 4; ++i) {
        DrawButtonBox(hdc, RECT{ 20 + i * 105, y, 120 + i * 105, y + 26 }, plNames[i], static_cast<int>(adv.nr.placement) == i);
    }
    y += 44;
    std::wstring resStr = std::wstring(adv.nr.residualEnabled ? L"[X] " : L"[  ] ") + s.residual;
    DrawButtonBox(hdc, RECT{ 20, y, 210, y + 26 }, resStr, adv.nr.residualEnabled);
    std::wstring multiStr = std::wstring(adv.nr.multipassEnabled ? L"[X] " : L"[  ] ") + s.multipass;
    DrawButtonBox(hdc, RECT{ 230, y, 420, y + 26 }, multiStr, adv.nr.multipassEnabled);
    y += 36;
    std::wstring mfgStr = std::wstring(adv.mfg.allowExperimental56x ? L"[X] " : L"[  ] ") + s.mfg56x;
    DrawButtonBox(hdc, RECT{ 20, y, 210, y + 26 }, mfgStr, adv.mfg.allowExperimental56x);
    std::wstring diagStr = std::wstring(adv.diagnostics.enabled ? L"[X] " : L"[  ] ") + s.diagnostics;
    DrawButtonBox(hdc, RECT{ 230, y, 420, y + 26 }, diagStr, adv.diagnostics.enabled);
    DrawButtonBox(hdc, RECT{ 110, rc.bottom - 50, 350, rc.bottom - 15 }, s.btnApply, true);
}

void RuntimeOverlayWindow::OnLButtonDown(HWND hwnd, int x, int y, RuntimeOverlay* owner) {
    if (x >= kMenuWidth - 35 && x <= kMenuWidth - 15 && y >= 14 && y <= 34) {
        owner->CloseMenu();
        return;
    }
    if (x >= kMenuWidth - 85 && x <= kMenuWidth - 45 && y >= 14 && y <= 34) {
        RuntimeLocalization::ToggleLanguage();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    if (y >= 50 && y <= 78) {
        if (x >= 20 && x <= 140) owner->MenuDrawing().SetActiveTab(MenuTab::Main);
        else if (x >= 150 && x <= 270) owner->MenuDrawing().SetActiveTab(MenuTab::Advanced);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    if (y >= kMenuHeight - 50 && y <= kMenuHeight - 15 && x >= 110 && x <= 350) {
        owner->ApplyStagedConfiguration();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    if (owner->MenuDrawing().ActiveTab() == MenuTab::Main) {
        auto draft = owner->MenuDrawing().MainDraft();
        if (x >= 20 && x <= 220 && y >= 95 && y <= 123) {
            draft.enabled = !draft.enabled;
            owner->MenuDrawing().StageMain(draft);
        } else if (y >= 159 && y <= 185) {
            for (int i = 0; i < 4; ++i) {
                if (x >= 20 + i * 105 && x <= 120 + i * 105) {
                    draft.mode = static_cast<RuntimeNrMode>(i);
                    owner->MenuDrawing().StageMain(draft);
                    break;
                }
            }
        } else if (y >= 199 && y <= 225) {
            if (x >= 240 && x <= 280) { draft.targetFps = (draft.targetFps > 30.0f) ? draft.targetFps - 5.0f : 30.0f; owner->MenuDrawing().StageMain(draft); }
            else if (x >= 290 && x <= 330) { draft.targetFps = (draft.targetFps < 240.0f) ? draft.targetFps + 5.0f : 240.0f; owner->MenuDrawing().StageMain(draft); }
        }
    } else {
        auto adv = owner->MenuDrawing().AdvancedDraft();
        if (y >= 119 && y <= 145) {
            for (int i = 0; i < 4; ++i) {
                if (x >= 20 + i * 105 && x <= 120 + i * 105) {
                    if (i == 0) adv.nr.precisionAuto = true;
                    else { adv.nr.precisionAuto = false; adv.nr.precision = static_cast<NrPrecision>(i - 1); }
                    owner->MenuDrawing().StageAdvanced(adv);
                    break;
                }
            }
        } else if (y >= 207 && y <= 233) {
            if (x >= 20 && x <= 210) { adv.nr.residualEnabled = !adv.nr.residualEnabled; owner->MenuDrawing().StageAdvanced(adv); }
            else if (x >= 230 && x <= 420) { adv.nr.multipassEnabled = !adv.nr.multipassEnabled; owner->MenuDrawing().StageAdvanced(adv); }
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

} // namespace nrfusion
