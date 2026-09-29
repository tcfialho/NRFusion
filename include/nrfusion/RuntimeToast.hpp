#pragma once

#include <string>
#include <cstdint>
#include <mutex>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace nrfusion {

enum class ToastType : std::uint8_t {
    Info,
    Progress,
    Success,
    Error
};

class RuntimeToast {
public:
    static RuntimeToast& Instance();

    void Initialize(HWND parent = nullptr);
    void Shutdown();

    void Show(const std::string& message, ToastType type, std::uint32_t durationMs = 3000);
    void Show(const std::wstring& message, ToastType type, std::uint32_t durationMs = 3000);
    void Hide();

    bool IsVisible() const noexcept;
    std::string CurrentMessage() const;
    std::wstring CurrentWideMessage() const;
    ToastType CurrentType() const noexcept;

private:
    RuntimeToast() = default;
    ~RuntimeToast();

    void EnsureWindow();
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint(HWND hwnd);

    mutable std::mutex mutex_;
    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    bool visible_ = false;
    std::string message_;
    std::wstring wideMessage_;
    ToastType type_ = ToastType::Info;
    std::uint32_t expireTick_ = 0;
};

} // namespace nrfusion
