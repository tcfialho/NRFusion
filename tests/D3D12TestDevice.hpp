#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace nrfusion::testing {

inline bool UseHardwareD3D12TestDevice() noexcept {
    char value[8]{};
    const DWORD size = GetEnvironmentVariableA(
        "NRFUSION_TEST_D3D12_HARDWARE", value,
        static_cast<DWORD>(sizeof(value)));
    return size != 0 && value[0] == '1';
}

inline Microsoft::WRL::ComPtr<ID3D12Device> CreateD3D12TestDevice() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return {};

    if (UseHardwareD3D12TestDevice()) {
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapterByGpuPreference(
                    index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&adapter))))
                break;
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) ||
                (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                continue;
            ComPtr<ID3D12Device> device;
            if (SUCCEEDED(D3D12CreateDevice(
                    adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device))))
                return device;
        }
        return {};
    }

    ComPtr<IDXGIAdapter> warp;
    if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) return {};
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
        return {};
    return device;
}

} // namespace nrfusion::testing
