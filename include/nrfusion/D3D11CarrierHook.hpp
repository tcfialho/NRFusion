#pragma once

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

#include "nrfusion/D3D11CarrierNativeAcquire.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace nrfusion {

class D3D11CarrierHook {
public:
    D3D11CarrierHook() = default;
    ~D3D11CarrierHook();

    D3D11CarrierHook(const D3D11CarrierHook&) = delete;
    D3D11CarrierHook& operator=(const D3D11CarrierHook&) = delete;

    bool Install(IDXGISwapChain* swapChain, ID3D11DeviceContext* context) noexcept;
    void Remove() noexcept;

    D3D11NativeAcquireResult AcquireLast(
        const ProviderInput& identity, Jitter jitter = {},
        bool hdr = false, bool cameraCut = false,
        bool resetHistory = false) noexcept;

    std::uint64_t CapturedFrames() const noexcept;
    bool Installed() const noexcept { return swapChainVtable_ != nullptr; }

private:
    using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
    static constexpr std::size_t kPresentIndex = 8;

    static HRESULT STDMETHODCALLTYPE HookedPresent(
        IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
    void Capture(IDXGISwapChain* swapChain) noexcept;
    bool Patch(void** slot, void* replacement, void*& previous) noexcept;
    void Restore(void** slot, void* original) noexcept;

    static std::atomic<D3D11CarrierHook*> active_;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11Resource> lastColor_;
    void** swapChainVtable_ = nullptr;
    PresentFn originalPresent_ = nullptr;
    std::uint64_t capturedFrames_ = 0;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
