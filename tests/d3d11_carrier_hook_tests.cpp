#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include "nrfusion/D3D11CarrierHook.hpp"

#include <cassert>

using Microsoft::WRL::ComPtr;
using namespace nrfusion;

namespace {

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_D3D11_HARDWARE", value,
        static_cast<DWORD>(sizeof(value))) != 0;
}

} // namespace

int main() {
    const wchar_t* className = L"NRFusionD3D11CarrierHookTest";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    assert(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

    HWND window = CreateWindowExW(
        0, className, L"NRFusion D3D11 x64 Hook", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 128, 128, nullptr, nullptr,
        wc.hInstance, nullptr);
    assert(window != nullptr);

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature{};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &swapChain, &device, &feature, &context);
    if (FAILED(hr) && !HardwareRequired()) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &swapChain, &device, &feature, &context);
    }
    assert(SUCCEEDED(hr));

    D3D11CarrierHook hook;
    assert(hook.Install(swapChain.Get(), context.Get()));
    assert(SUCCEEDED(swapChain->Present(0, 0)));
    assert(hook.CapturedFrames() == 1);

    ProviderInput identity{};
    identity.frameId = 1;
    identity.configurationGeneration = 1;
    const auto first = hook.AcquireLast(identity);
    assert(first);
    const Resolution firstResolution{64, 64};
    assert(first.frame.renderResolution == firstResolution);
    assert(!hook.AcquireLast(identity));

    context->ClearState();
    context->Flush();
    assert(SUCCEEDED(swapChain->ResizeBuffers(
        1, 96, 64, DXGI_FORMAT_R16G16B16A16_FLOAT, 0)));
    assert(SUCCEEDED(swapChain->Present(0, 0)));
    assert(hook.CapturedFrames() == 2);
    identity.frameId = 2;
    const auto resized = hook.AcquireLast(identity);
    assert(resized);
    const Resolution resizedResolution{96, 64};
    assert(resized.frame.renderResolution == resizedResolution);

    hook.Remove();
    assert(!hook.Installed());
    assert(SUCCEEDED(swapChain->Present(0, 0)));
    identity.frameId = 3;
    assert(!hook.AcquireLast(identity));

    DestroyWindow(window);
    return 0;
}
