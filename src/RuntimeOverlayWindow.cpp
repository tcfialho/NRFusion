#include "RuntimeOverlayWindow.hpp"
#include "RuntimeOverlayD3D12.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/GameWindowFinder.hpp"
#include "nrfusion/Logger.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace nrfusion {
namespace {
constexpr const wchar_t* kOverlayClassName = L"NRFusion_Overlay_Menu";
constexpr int kWindowWidth = 540;
constexpr int kWindowHeight = 440;
using Microsoft::WRL::ComPtr;

struct WindowRenderer {
    HWND hwnd = nullptr;
    ImGuiContext* imgui = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTarget;
};

WindowRenderer g_renderer;

bool CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (!g_renderer.swapChain || FAILED(g_renderer.swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return false;
    return SUCCEEDED(g_renderer.device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderer.renderTarget));
}

bool InitializeRenderer(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selected{};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc,
            &g_renderer.swapChain, &g_renderer.device, &selected, &g_renderer.context))) return false;
    if (!CreateRenderTarget()) return false;

    g_renderer.imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_renderer.imgui);
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX11_Init(g_renderer.device.Get(), g_renderer.context.Get())) return false;
    g_renderer.hwnd = hwnd;
    SetTimer(hwnd, 1, 16, nullptr);
    NRF_LOG_INFO("OverlayWindow", "Standalone ImGui ready hwnd=%p", hwnd);
    return true;
}

void ShutdownRenderer() {
    if (g_renderer.hwnd) KillTimer(g_renderer.hwnd, 1);
    if (g_renderer.imgui) {
        ImGui::SetCurrentContext(g_renderer.imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(g_renderer.imgui);
    }
    g_renderer = {};
}

void ResizeRenderer(UINT width, UINT height) {
    if (!g_renderer.swapChain || !width || !height) return;
    g_renderer.renderTarget.Reset();
    if (SUCCEEDED(g_renderer.swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) CreateRenderTarget();
}

void Render(RuntimeOverlay* owner) {
    if (!owner || !owner->IsMenuOpen() || !g_renderer.renderTarget || !g_renderer.imgui) return;
    ImGui::SetCurrentContext(g_renderer.imgui);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawRuntimeOverlayImGui(true);
    ImGui::Render();
    const float clear[] = {0.02f, 0.02f, 0.025f, 1.0f};
    ID3D11RenderTargetView* target = g_renderer.renderTarget.Get();
    g_renderer.context->OMSetRenderTargets(1, &target, nullptr);
    g_renderer.context->ClearRenderTargetView(target, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_renderer.swapChain->Present(1, 0);
}
}

HWND RuntimeOverlayWindow::Create(RuntimeOverlay* owner, HWND parent) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassExW(&wc);

    HWND targetParent = parent && IsWindow(parent) ? parent : FindGameWindow();
    int x = 60;
    int y = 60;
    if (targetParent) {
        RECT rc{};
        if (GetWindowRect(targetParent, &rc)) { x = rc.left + 50; y = rc.top + 50; }
    }
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClassName, L"NRFusion",
        WS_POPUP, x, y, kWindowWidth, kWindowHeight,
        targetParent, nullptr, instance, owner);
    if (!hwnd || !InitializeRenderer(hwnd)) {
        if (hwnd) DestroyWindow(hwnd);
        ShutdownRenderer();
        return nullptr;
    }
    return hwnd;
}

void RuntimeOverlayWindow::Destroy(HWND hwnd) {
    ShutdownRenderer();
    if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
}

LRESULT CALLBACK RuntimeOverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST) {
        POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        RECT rect{};
        if (GetWindowRect(hwnd, &rect)) {
            const int x = point.x - rect.left;
            const int y = point.y - rect.top;
            if (y >= 0 && y < 28 && x >= 0 && x < (rect.right - rect.left - 34)) return HTCAPTION;
        }
    }
    if (g_renderer.imgui) {
        ImGui::SetCurrentContext(g_renderer.imgui);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 1;
    }
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* owner = reinterpret_cast<RuntimeOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) ResizeRenderer(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_TIMER:
    case WM_PAINT:
        Render(owner);
        if (msg == WM_PAINT) { PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }
        return 0;
    case WM_CLOSE:
        if (owner) owner->CloseMenu();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace nrfusion
