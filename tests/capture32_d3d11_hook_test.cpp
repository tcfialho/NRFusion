#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "nrfusion/CaptureProvider32Export.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

struct SpawnedHost {
    HANDLE process = nullptr;

    ~SpawnedHost() {
        if (!process) return;
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 2000);
        CloseHandle(process);
    }
};

LRESULT CALLBACK TestWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

bool Fail(const char* message) {
    std::cerr << "[Capture32 D3D11 Hook] FAIL: " << message << "\n";
    return false;
}

std::string ResolveHostPath() {
    char configuredPath[MAX_PATH]{};
    const DWORD configuredLength = GetEnvironmentVariableA("NRFUSION_HOST64_PATH", configuredPath,
                                                            static_cast<DWORD>(sizeof(configuredPath)));
    return configuredLength > 0 && configuredLength < sizeof(configuredPath) ? configuredPath : std::string{};
}

bool StartHost(SpawnedHost& host) {
    const std::string hostPath = ResolveHostPath();
    if (hostPath.empty() || GetFileAttributesA(hostPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return Fail("set NRFUSION_HOST64_PATH to the x64 NRFusionHost64.exe");
    }

    std::string commandLine = "\"" + hostPath + "\" " + std::to_string(GetCurrentProcessId());
    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return Fail("CreateProcessA for NRFusionHost64.exe failed");
    }
    CloseHandle(process.hThread);
    host.process = process.hProcess;
    return true;
}

bool PresentColor(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context,
                  const float color[4]) {
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    return SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) &&
        SUCCEEDED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget)) &&
        (context->ClearRenderTargetView(renderTarget.Get(), color), true) &&
        SUCCEEDED(swapChain->Present(0, 0));
}

// Binds a real depth-stencil view (created typeless, the only shape the capture hook can read as
// an SRV) through OMSetRenderTargets before presenting, exercising the depth capture heuristic.
bool PresentColorWithDepth(IDXGISwapChain* swapChain, ID3D11Device* device, ID3D11DeviceContext* context,
                           ID3D11DepthStencilView* depthStencilView, const float color[4], float depthValue) {
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) ||
        FAILED(device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTarget))) {
        return false;
    }
    ID3D11RenderTargetView* rtvs[1] = { renderTarget.Get() };
    context->OMSetRenderTargets(1, rtvs, depthStencilView);
    context->ClearRenderTargetView(renderTarget.Get(), color);
    context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH, depthValue, 0);
    return SUCCEEDED(swapChain->Present(0, 0));
}

} // namespace

int main() {
    std::cout << "[Capture32 D3D11 Hook] Win32 IAT -> Present -> resize smoke\n";
    SpawnedHost host;
    if (!StartHost(host)) return 1;

    const wchar_t* className = L"NRFusionCapture32D3D11HookTest";
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = TestWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return Fail("RegisterClassW failed") ? 0 : 1;
    }
    HWND window = CreateWindowExW(0, className, L"NRFusion Capture32 Hook Test", WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT, CW_USEDEFAULT, 128, 128, nullptr, nullptr,
                                   windowClass.hInstance, nullptr);
    if (!window) return Fail("CreateWindowExW failed") ? 0 : 1;

    // The DLL worker has to patch the executable's D3D11 IAT before this first creation call.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    NRFusion_Capture32_SetWorkingScale(0.5f);

    DXGI_SWAP_CHAIN_DESC swapDescription{};
    swapDescription.BufferDesc.Width = 64;
    swapDescription.BufferDesc.Height = 64;
    swapDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = 1;
    swapDescription.OutputWindow = window;
    swapDescription.Windowed = TRUE;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel{};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &swapDescription, &swapChain, &device,
                                             &featureLevel, &context))) {
        DestroyWindow(window);
        return Fail("D3D11CreateDeviceAndSwapChain failed") ? 0 : 1;
    }

    const float firstColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
    for (int frame = 0; frame != 3; ++frame) {
        if (!PresentColor(swapChain.Get(), device.Get(), context.Get(), firstColor)) {
            DestroyWindow(window);
            return Fail("Present before resize failed") ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!NRFusion_Capture32_IsConnected()) {
        DestroyWindow(window);
        return Fail("Present hook did not configure the capture client") ? 0 : 1;
    }
    nrfusion::CaptureD3D11TransportInfo transportInfo{};
    if (!NRFusion_Capture32_GetTransportInfo(&transportInfo) || !transportInfo.configured ||
        transportInfo.nativeWidth != 64 || transportInfo.nativeHeight != 64 ||
        transportInfo.workWidth != 32 || transportInfo.workHeight != 32 ||
        transportInfo.transportFormat != static_cast<uint32_t>(DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        transportInfo.downsampleDispatches == 0 ||
        transportInfo.composeDispatches == 0) {
        DestroyWindow(window);
        return Fail("WorkingScale did not produce downsample/compose dispatches") ? 0 : 1;
    }
    if (transportInfo.hasDepthGuide) {
        DestroyWindow(window);
        return Fail("depth guide reported available with no depth-stencil view ever bound") ? 0 : 1;
    }
    const uint64_t sessionBeforeResize = NRFusion_Capture32_ActiveSessionId();

    context->ClearState();
    context->Flush();
    if (FAILED(swapChain->ResizeBuffers(1, 96, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0))) {
        DestroyWindow(window);
        return Fail("ResizeBuffers failed") ? 0 : 1;
    }

    // A typeless depth resource so the hook can create the D3D11_SRV_DIMENSION_TEXTURE2D view it
    // needs (a resource created directly as D32_FLOAT could not be read back this way).
    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 96;
    depthDesc.Height = 64;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> depthTexture;
    if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, &depthTexture))) {
        DestroyWindow(window);
        return Fail("depth-stencil texture creation failed") ? 0 : 1;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> depthStencilView;
    if (FAILED(device->CreateDepthStencilView(depthTexture.Get(), &dsvDesc, &depthStencilView))) {
        DestroyWindow(window);
        return Fail("depth-stencil view creation failed") ? 0 : 1;
    }

    const float secondColor[] = {0.0f, 1.0f, 0.0f, 1.0f};
    bool reconnected = false;
    for (int frame = 0; frame != 8; ++frame) {
        if (!PresentColorWithDepth(swapChain.Get(), device.Get(), context.Get(), depthStencilView.Get(),
                                   secondColor, 0.5f)) {
            DestroyWindow(window);
            return Fail("Present after resize failed") ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (NRFusion_Capture32_IsConnected() &&
            NRFusion_Capture32_ActiveSessionId() > sessionBeforeResize) {
            reconnected = true;
            nrfusion::CaptureD3D11TransportInfo checkInfo{};
            if (NRFusion_Capture32_GetTransportInfo(&checkInfo) && checkInfo.composeDispatches > 0 &&
                checkInfo.depthCaptureDispatches > 0) {
                break;
            }
        }
    }

    nrfusion::CaptureD3D11TransportInfo resizedInfo{};
    const bool resizedInfoValid = NRFusion_Capture32_GetTransportInfo(&resizedInfo) &&
        resizedInfo.nativeWidth == 96 && resizedInfo.nativeHeight == 64 &&
        resizedInfo.workWidth == 48 && resizedInfo.workHeight == 32 &&
        resizedInfo.downsampleDispatches > 0 &&
        resizedInfo.composeDispatches > 0;
    const bool depthCaptureValid = resizedInfo.hasDepthGuide && resizedInfo.depthCaptureDispatches > 0;

    NRFusion_Capture32_Disconnect();
    DestroyWindow(window);
    if (!reconnected || !resizedInfoValid) {
        return Fail("ResizeBuffers did not recreate the reduced transport generation") ? 0 : 1;
    }
    if (!depthCaptureValid) {
        return Fail("real depth-stencil view was bound but the capture hook did not pick it up") ? 0 : 1;
    }
    std::cout << "[Capture32 D3D11 Hook] PASS: IAT, Present/Resize and depth-stencil capture hooks reached "
                 "the capture transport.\n";

    // Split creation path: D3D11CreateDevice + IDXGIFactory1::CreateSwapChain, the pattern flip-
    // model/HDR engines use instead of the monolithic D3D11CreateDeviceAndSwapChain exercised
    // above. Reaching this point with the transport re-configured for the new resolution proves
    // the dxgi.dll IAT hooks (CreateDXGIFactory/1/2) and the factory vtable patch attach correctly.
    std::cout << "[Capture32 D3D11 Hook] DXGI factory CreateSwapChain smoke\n";
    HWND splitWindow = CreateWindowExW(0, className, L"NRFusion Capture32 Split Path Test", WS_OVERLAPPEDWINDOW,
                                        CW_USEDEFAULT, CW_USEDEFAULT, 128, 128, nullptr, nullptr,
                                        windowClass.hInstance, nullptr);
    if (!splitWindow) return Fail("CreateWindowExW (split path) failed") ? 0 : 1;

    ComPtr<ID3D11Device> splitDevice;
    ComPtr<ID3D11DeviceContext> splitContext;
    D3D_FEATURE_LEVEL splitFeatureLevel{};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
                                 &splitDevice, &splitFeatureLevel, &splitContext))) {
        DestroyWindow(splitWindow);
        return Fail("D3D11CreateDevice (split path) failed") ? 0 : 1;
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        DestroyWindow(splitWindow);
        return Fail("CreateDXGIFactory1 failed") ? 0 : 1;
    }

    DXGI_SWAP_CHAIN_DESC splitSwapDescription{};
    splitSwapDescription.BufferDesc.Width = 48;
    splitSwapDescription.BufferDesc.Height = 48;
    splitSwapDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    splitSwapDescription.SampleDesc.Count = 1;
    splitSwapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    splitSwapDescription.BufferCount = 1;
    splitSwapDescription.OutputWindow = splitWindow;
    splitSwapDescription.Windowed = TRUE;
    splitSwapDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ComPtr<IDXGISwapChain> splitSwapChain;
    if (FAILED(factory->CreateSwapChain(splitDevice.Get(), &splitSwapDescription, &splitSwapChain))) {
        DestroyWindow(splitWindow);
        return Fail("IDXGIFactory1::CreateSwapChain failed") ? 0 : 1;
    }

    const float splitColor[] = {0.0f, 0.0f, 1.0f, 1.0f};
    bool splitAttached = false;
    for (int frame = 0; frame != 8; ++frame) {
        if (!PresentColor(splitSwapChain.Get(), splitDevice.Get(), splitContext.Get(), splitColor)) {
            DestroyWindow(splitWindow);
            return Fail("Present on the split-path swap chain failed") ? 0 : 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        nrfusion::CaptureD3D11TransportInfo splitInfo{};
        if (NRFusion_Capture32_IsConnected() && NRFusion_Capture32_GetTransportInfo(&splitInfo) &&
            splitInfo.configured && splitInfo.nativeWidth == 48 && splitInfo.nativeHeight == 48) {
            splitAttached = true;
            break;
        }
    }

    NRFusion_Capture32_Disconnect();
    DestroyWindow(splitWindow);
    if (!splitAttached) {
        return Fail("split D3D11CreateDevice + IDXGIFactory::CreateSwapChain path never attached the capture "
                    "session (dxgi.dll IAT/vtable hook coverage gap)") ? 0 : 1;
    }

    std::cout << "[Capture32 D3D11 Hook] PASS: DXGI factory CreateSwapChain path reached the capture transport.\n";
    return 0;
}
