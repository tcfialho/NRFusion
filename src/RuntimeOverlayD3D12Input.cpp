#include "RuntimeOverlayD3D12.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/Logger.hpp"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <windowsx.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace nrfusion {

bool RuntimeOverlayD3D12::InstallInput(HWND window) {
    std::lock_guard lock(inputMutex_);
    window_ = window;
    SetLastError(0);
    originalWndProc_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GameWndProc)));
    if (!originalWndProc_) {
        NRF_LOG_ERROR("OverlayD3D12", "Game input subclass failed error=%lu", GetLastError());
        window_ = nullptr;
        return false;
    }
    return true;
}

void RuntimeOverlayD3D12::RemoveInput() {
    std::lock_guard lock(inputMutex_);
    if (window_ && IsWindow(window_) && originalWndProc_ &&
        GetWindowLongPtrW(window_, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&GameWndProc)) {
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc_));
    }
    inputMessages_.clear();
}

LRESULT CALLBACK RuntimeOverlayD3D12::GameWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto& renderer = Instance();
    WNDPROC original = nullptr;
    const bool open = RuntimeOverlay::Instance().IsMenuOpen();
    const bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
    const bool keyboard = message >= WM_KEYFIRST && message <= WM_KEYLAST;
    {
        std::lock_guard lock(renderer.inputMutex_);
        original = renderer.originalWndProc_;
        if (open && (mouse || keyboard || message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
            if (renderer.inputMessages_.size() < 256)
                renderer.inputMessages_.push_back(MSG{window, message, wParam, lParam});
        }
    }
    if (open) {
        if (message == WM_INPUT) return DefWindowProcW(window, message, wParam, lParam);
        if (mouse || keyboard) return 0;
        if (message == WM_SETCURSOR) {
            SetCursor(nullptr);
            return TRUE;
        }
    }
    return original ? CallWindowProcW(original, window, message, wParam, lParam)
                    : DefWindowProcW(window, message, wParam, lParam);
}

void RuntimeOverlayD3D12::FeedInput() {
    RECT client{};
    if (!GetClientRect(window_, &client) || client.right <= 0 || client.bottom <= 0 || frames_.empty()) return;
    const auto buffer = frames_[0].backBuffer->GetDesc();
    const float scaleX = static_cast<float>(buffer.Width) / client.right;
    const float scaleY = static_cast<float>(buffer.Height) / client.bottom;
    std::deque<MSG> messages;
    {
        std::lock_guard lock(inputMutex_);
        messages.swap(inputMessages_);
    }
    for (auto message : messages) {
        if (message.message == WM_MOUSEMOVE) {
            const int x = static_cast<int>(GET_X_LPARAM(message.lParam) * scaleX);
            const int y = static_cast<int>(GET_Y_LPARAM(message.lParam) * scaleY);
            message.lParam = MAKELPARAM(x, y);
        }
        ImGui_ImplWin32_WndProcHandler(message.hwnd, message.message, message.wParam, message.lParam);
    }
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(buffer.Width), static_cast<float>(buffer.Height));
    POINT cursor{};
    if (GetCursorPos(&cursor) && ScreenToClient(window_, &cursor))
        io.AddMousePosEvent(cursor.x * scaleX, cursor.y * scaleY);
}

} // namespace nrfusion
