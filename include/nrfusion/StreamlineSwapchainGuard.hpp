#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_2.h>
#include <atomic>

namespace nrfusion {

class StreamlineSwapchainGuard {
public:
    static StreamlineSwapchainGuard& Instance() noexcept;

    bool Install(HMODULE targetModule = nullptr) noexcept;

    void OnPostCreateSwapChainForHwnd(
        IDXGIFactory2* factory,
        IUnknown* device,
        HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
        IDXGIOutput* restrictToOutput,
        IDXGISwapChain1** swapChain) noexcept;

    void OnPostCreateSwapChain(
        IDXGIFactory* factory,
        IUnknown* device,
        const DXGI_SWAP_CHAIN_DESC* desc,
        IDXGISwapChain** swapChain) noexcept;

    void Reset() noexcept;
    bool IsDlssgGuarded() const noexcept { return dlssgGuarded_.load(); }
    bool IsCommonGuarded() const noexcept { return commonGuarded_.load(); }

private:
    StreamlineSwapchainGuard() = default;
    bool InstallCommonGuards(HMODULE commonModule) noexcept;
    bool InstallDlssgGuards(HMODULE dlssgModule) noexcept;

    std::atomic<bool> dlssgGuarded_{false};
    std::atomic<bool> commonGuarded_{false};
};

} // namespace nrfusion
