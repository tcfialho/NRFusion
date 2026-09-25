#include "D3D9ExShareHarness.hpp"

#include <iterator>

namespace nrfusion::test {
namespace {

constexpr wchar_t kWindowClass[] =
    L"NRFusionD3D9ExShareHarness";

LRESULT CALLBACK WindowProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

bool SameLuid(const LUID& left, const LUID& right) noexcept {
    return left.LowPart == right.LowPart &&
           left.HighPart == right.HighPart;
}

} // namespace

D3DPRESENT_PARAMETERS
D3D9ExShareHarness::PresentParameters() const noexcept {
    D3DPRESENT_PARAMETERS params{};
    params.BackBufferWidth = 64;
    params.BackBufferHeight = 64;
    params.BackBufferFormat = D3DFMT_UNKNOWN;
    params.BackBufferCount = 1;
    params.MultiSampleType = D3DMULTISAMPLE_NONE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = window_;
    params.Windowed = TRUE;
    params.PresentationInterval =
        D3DPRESENT_INTERVAL_IMMEDIATE;
    return params;
}

bool D3D9ExShareHarness::CreateWindowAndDevice9() {
    WNDCLASSW windowClass{};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassW(&windowClass) != 0) {
        classRegistered_ = true;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        0, kWindowClass, L"NRFusion D3D9Ex",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 64, 64,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window_) return false;

    if (FAILED(Direct3DCreate9Ex(
            D3D_SDK_VERSION, &d3d9_)) ||
        !d3d9_) {
        return false;
    }

    auto params = PresentParameters();
    HRESULT created = d3d9_->CreateDeviceEx(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        window_,
        D3DCREATE_HARDWARE_VERTEXPROCESSING |
            D3DCREATE_MULTITHREADED,
        &params, nullptr, &device9_);
    if (FAILED(created)) {
        params = PresentParameters();
        created = d3d9_->CreateDeviceEx(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            window_,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING |
                D3DCREATE_MULTITHREADED,
            &params, nullptr, &device9_);
    }
    return SUCCEEDED(created) && device9_ != nullptr;
}

bool D3D9ExShareHarness::CreateDxgiDevices() {
    if (!d3d9_) return false;

    LUID d3d9Luid{};
    if (FAILED(d3d9_->GetAdapterLUID(
            D3DADAPTER_DEFAULT, &d3d9Luid))) {
        return false;
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumerated =
            factory->EnumAdapters1(index, &candidate);
        if (enumerated == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumerated)) return false;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(candidate->GetDesc1(&desc)) ||
            !SameLuid(desc.AdapterLuid, d3d9Luid)) {
            continue;
        }

        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0};
        if (FAILED(D3D11CreateDevice(
                candidate.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr, 0,
                levels,
                static_cast<UINT>(std::size(levels)),
                D3D11_SDK_VERSION,
                &device11_, nullptr,
                &context11_))) {
            return false;
        }
        if (FAILED(D3D12CreateDevice(
                candidate.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device12_)))) {
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device12_->CreateCommandQueue(
                &queueDesc, IID_PPV_ARGS(&queue12_)))) {
            return false;
        }
        adapter_ = candidate;
        return fenceBridge_.BindAfterIdle(
            device11_.Get(), context11_.Get(),
            device12_.Get(), queue12_.Get());
    }
    return false;
}

bool D3D9ExShareHarness::Open() {
    Close();
    if (!CreateWindowAndDevice9() ||
        !CreateDxgiDevices()) {
        Close();
        return false;
    }
    return true;
}

void D3D9ExShareHarness::Close() noexcept {
    fenceBridge_.ResetAfterIdle();
    ntBridge12_.Reset();
    ntMutex11_.Reset();
    ntBridge11_.Reset();
    sharedTexture11_.Reset();
    eventQuery9_.Reset();
    sharedTexture9_.Reset();
    sharedHandle9_ = nullptr;
    queue12_.Reset();
    device12_.Reset();
    context11_.Reset();
    device11_.Reset();
    adapter_.Reset();
    device9_.Reset();
    d3d9_.Reset();
    width_ = 0;
    height_ = 0;

    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (classRegistered_) {
        UnregisterClassW(
            kWindowClass, GetModuleHandleW(nullptr));
        classRegistered_ = false;
    }
}

} // namespace nrfusion::test
