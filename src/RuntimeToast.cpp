#include "nrfusion/RuntimeToast.hpp"
#include "nrfusion/GameWindowFinder.hpp"
#include "nrfusion/Logger.hpp"
#include <commctrl.h>

namespace nrfusion {

namespace {
constexpr const wchar_t* kToastClassName = L"NRFusion_Toast_Window";
constexpr int kToastWidth = 320;
constexpr int kToastHeight = 56;
constexpr int kMarginRight = 32;
constexpr int kMarginBottom = 48;
} // namespace

RuntimeToast& RuntimeToast::Instance() {
    static RuntimeToast instance;
    return instance;
}

RuntimeToast::~RuntimeToast() {
    Shutdown();
}

void RuntimeToast::Initialize(HWND parent) {
    std::lock_guard<std::mutex> lock(mutex_);
    parent_ = parent;
}

void RuntimeToast::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hwnd_ && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    visible_ = false;
}

void RuntimeToast::EnsureWindow() {
    if (hwnd_ && IsWindow(hwnd_)) {
        return;
    }

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kToastClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassExW(&wc);

    HWND targetParent = parent_;
    if (!targetParent || !IsWindow(targetParent)) {
        targetParent = FindGameWindow();
        parent_ = targetParent;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = screenW - kToastWidth - kMarginRight;
    int y = screenH - kToastHeight - kMarginBottom;

    if (targetParent && IsWindow(targetParent)) {
        RECT prc;
        if (GetWindowRect(targetParent, &prc)) {
            x = prc.right - kToastWidth - kMarginRight;
            y = prc.bottom - kToastHeight - kMarginBottom;
        }
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        kToastClassName, L"NRFusion Toast",
        WS_POPUP,
        x, y, kToastWidth, kToastHeight,
        targetParent, nullptr, hInst, this
    );

    if (hwnd_) {
        SetLayeredWindowAttributes(hwnd_, 0, 240, LWA_ALPHA);
        NRF_LOG_INFO("Toast", "Created toast window hwnd=%p owned by parent=%p", hwnd_, targetParent);
    }
}

void RuntimeToast::Show(const std::string& message, ToastType type, std::uint32_t durationMs) {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring wmsg;
    if (wideLen > 1) {
        wmsg.resize(static_cast<size_t>(wideLen - 1));
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, &wmsg[0], wideLen);
    }
    Show(wmsg, type, durationMs);
    std::lock_guard<std::mutex> lock(mutex_);
    message_ = message;
}

void RuntimeToast::Show(const std::wstring& message, ToastType type, std::uint32_t durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    EnsureWindow();
    wideMessage_ = message;
    int len = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len > 1) {
        message_.resize(static_cast<size_t>(len - 1));
        WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, &message_[0], len, nullptr, nullptr);
    } else {
        message_.clear();
    }
    type_ = type;
    visible_ = true;
    expireTick_ = (durationMs > 0) ? (GetTickCount() + durationMs) : 0;

    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, TRUE);
        if (durationMs > 0) {
            SetTimer(hwnd_, 1, durationMs, nullptr);
        } else {
            KillTimer(hwnd_, 1);
        }
    }
}

void RuntimeToast::Hide() {
    std::lock_guard<std::mutex> lock(mutex_);
    visible_ = false;
    if (hwnd_ && IsWindow(hwnd_)) {
        ShowWindow(hwnd_, SW_HIDE);
        KillTimer(hwnd_, 1);
    }
}

bool RuntimeToast::IsVisible() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return visible_;
}

std::string RuntimeToast::CurrentMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return message_;
}

std::wstring RuntimeToast::CurrentWideMessage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return wideMessage_;
}

ToastType RuntimeToast::CurrentType() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return type_;
}

LRESULT CALLBACK RuntimeToast::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    auto* self = reinterpret_cast<RuntimeToast*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_PAINT:
        if (self) {
            self->OnPaint(hwnd);
        }
        return 0;
    case WM_TIMER:
        if (wParam == 1 && self) {
            self->Hide();
        }
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void RuntimeToast::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    auto oldBitmap = SelectObject(memDC, memBitmap);

    // Dark sleek container
    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 18, 22));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Accent line on left edge depending on type
    COLORREF accentColor = RGB(140, 140, 140);
    if (type_ == ToastType::Success) accentColor = RGB(46, 204, 113);
    else if (type_ == ToastType::Progress) accentColor = RGB(52, 152, 219);
    else if (type_ == ToastType::Error) accentColor = RGB(231, 76, 60);

    RECT accentRc = { 0, 0, 5, rc.bottom };
    HBRUSH accentBrush = CreateSolidBrush(accentColor);
    FillRect(memDC, &accentRc, accentBrush);
    DeleteObject(accentBrush);

    // Subtle border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 52));
    auto oldPen = SelectObject(memDC, borderPen);
    auto oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
    Rectangle(memDC, 0, 0, rc.right, rc.bottom);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(borderPen);

    // Text rendering
    HFONT hFont = CreateFontW(
        16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    auto oldFont = SelectObject(memDC, hFont);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(240, 240, 245));

    RECT textRc = { 16, 0, rc.right - 12, rc.bottom };
    std::wstring wmsg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wmsg = wideMessage_;
    }
    if (!wmsg.empty()) {
        DrawTextW(memDC, wmsg.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(memDC, oldFont);
    DeleteObject(hFont);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

} // namespace nrfusion
