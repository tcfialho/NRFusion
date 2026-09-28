#include "nrfusion/CaptureD3D11.hpp"

#include "nrfusion/CaptureProvider32Export.h"
#include "nrfusion/MatchedResidualShader.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <tlhelp32.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nrfusion {
namespace {

using Microsoft::WRL::ComPtr;

using CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                    const D3D_FEATURE_LEVEL*, UINT, UINT,
                                                    const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                                    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*,
                                                       ID3D11DepthStencilView*);
// CreateDXGIFactory and CreateDXGIFactory1 share this signature; only the ...2 variant adds flags.
using CreateDxgiFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
using CreateDxgiFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
using DxgiCreateSwapChainFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*,
                                                          IDXGISwapChain**);
using DxgiCreateSwapChainForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
                                                                  const DXGI_SWAP_CHAIN_DESC1*,
                                                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
                                                                  IDXGIOutput*, IDXGISwapChain1**);

constexpr size_t kSwapChainPresentIndex = 8;
constexpr size_t kSwapChainResizeBuffersIndex = 13;
constexpr size_t kContextOMSetRenderTargetsIndex = 33;
// IDXGIObject (slots 3-6) + IDXGIFactory's own EnumAdapters/MakeWindowAssociation/
// GetWindowAssociation put CreateSwapChain at 10. IDXGIFactory1 adds EnumAdapters1/IsCurrent
// (12-13), then IDXGIFactory2 adds IsWindowedStereoEnabled(14) before CreateSwapChainForHwnd(15).
constexpr size_t kFactoryCreateSwapChainIndex = 10;
constexpr size_t kFactory2CreateSwapChainForHwndIndex = 15;

struct VtableOriginals {
    PresentFn present = nullptr;
    ResizeBuffersFn resizeBuffers = nullptr;
};

struct ContextVtableOriginals {
    OMSetRenderTargetsFn omSetRenderTargets = nullptr;
};

struct FactoryVtableOriginals {
    DxgiCreateSwapChainFn createSwapChain = nullptr;
    // Left null when the factory has no IDXGIFactory2 vtable slot to patch (pre-Factory2 objects).
    DxgiCreateSwapChainForHwndFn createSwapChainForHwnd = nullptr;
};

// Defined after D3D11CaptureSession (alongside the swapchain vtable bookkeeping it mirrors);
// forward-declared so the session can call through to the untouched OMSetRenderTargets when it
// needs to detach/restore the output-merger around a depth SRV read.
OMSetRenderTargetsFn OriginalOMSetRenderTargets(ID3D11DeviceContext* context);

struct IatPatch {
    void** slot = nullptr;
    void* original = nullptr;
};

#include "CaptureD3D11Session.inc"
#include "CaptureD3D11HookSetup.inc"
#include "CaptureD3D11HookRuntime.inc"

} // namespace nrfusion
