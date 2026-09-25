#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/D3D11D3D12FenceBridge.hpp"

#include <cstdint>

namespace nrfusion::test {

using Microsoft::WRL::ComPtr;

class D3D9ExShareHarness {
public:
    bool Open();
    void Close() noexcept;

    bool ProveSharedTexture(
        std::uint32_t width,
        std::uint32_t height);
    bool ProveResetPersistence();

private:
    bool CreateWindowAndDevice9();
    bool CreateDxgiDevices();
    bool CreateD3D11NtBridge(
        std::uint32_t width,
        std::uint32_t height);
    D3DPRESENT_PARAMETERS PresentParameters() const noexcept;

    HWND window_ = nullptr;
    bool classRegistered_ = false;
    ComPtr<IDirect3D9Ex> d3d9_;
    ComPtr<IDirect3DDevice9Ex> device9_;
    ComPtr<IDirect3DTexture9> sharedTexture9_;
    ComPtr<IDirect3DQuery9> eventQuery9_;
    HANDLE sharedHandle9_ = nullptr;

    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D11Device> device11_;
    ComPtr<ID3D11DeviceContext> context11_;
    ComPtr<ID3D11Texture2D> sharedTexture11_;
    ComPtr<ID3D11Texture2D> ntBridge11_;
    ComPtr<IDXGIKeyedMutex> ntMutex11_;
    ComPtr<ID3D12Device> device12_;
    ComPtr<ID3D12Resource> ntBridge12_;
    ComPtr<ID3D12CommandQueue> queue12_;
    D3D11D3D12FenceBridge fenceBridge_{};

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace nrfusion::test
