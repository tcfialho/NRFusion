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

#include <atomic>
#include <algorithm>
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

class D3D11CaptureSession {
public:
    void SetWorkingScale(float workingScale) {
        if (!std::isfinite(workingScale)) return;
        const float resolved = std::clamp(workingScale, 0.5f, 1.0f);
        std::scoped_lock lock(mutex_);
        if (std::abs(workingScale_ - resolved) > 0.0001f) {
            workingScale_ = resolved;
            if (configured_) InvalidateLocked();
        }
    }

    void SetProcessingMode(uint32_t mode) {
        std::scoped_lock lock(mutex_);
        if (processingMode_ != mode) {
            processingMode_ = mode;
            if (configured_) InvalidateLocked();
        }
    }

    void Attach(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain) {
        std::scoped_lock lock(mutex_);
        if (device_.Get() != device || swapChain_ != swapChain) {
            InvalidateLocked();
            device_ = device;
            context_ = context;
            swapChain_ = swapChain;
        }
    }

    void OnPresent(IDXGISwapChain* swapChain) {
        std::scoped_lock lock(mutex_);
        if (!swapChain) return;

        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) return;
        if (!EnsureTransportLocked(swapChain, backBuffer.Get())) return;

        if (depthAvailable_ && depthMutex_ && depthMutex_->AcquireSync(0, 0) == S_OK) {
            RecordCaptureDepthLocked();
            depthMutex_->ReleaseSync(0);
        }

        if (colorMutex_->AcquireSync(0, 0) != S_OK) return;
        const bool isNeural = processingMode_ == static_cast<uint32_t>(IpcProcessingMode::Neural);
        const bool needsConversion = (workingScale_ < 0.999f) ||
            (isNeural && nativeFormat_ != DXGI_FORMAT_R16G16B16A16_FLOAT);
        const bool prepared = needsConversion
            ? RecordDownsampleLocked(backBuffer.Get())
            : (context_->CopyResource(sharedColor_.Get(), backBuffer.Get()), true);
        const uint64_t inputValue = nextInputFenceValue_++;
        const uint64_t outputValue = nextOutputFenceValue_++;
        if (!prepared || FAILED(context4_->Signal(inputFence_.Get(), inputValue))) {
            colorMutex_->ReleaseSync(0);
            InvalidateLocked();
            return;
        }
        colorMutex_->ReleaseSync(0);

        PipelinedFrameResult completed{};
        const uint64_t workId = nextWorkId_++;
        if (!NRFusion_Capture32_SubmitFramePipelinedEx(workId, 0, 0, inputValue, outputValue,
                                                       0.0f, 0.0f, false, &completed)) {
            // A bounded IPC write can still be pending. Keep the session and drop only this
            // frame; reconnect is reserved for a real pipe fault detected by the client API.
            if (!NRFusion_Capture32_IsConnected()) InvalidateLocked();
            return;
        }

        // This is a GPU queue wait, not a CPU wait. It consumes only an acknowledgement that the
        // client observed before submitting this frame, so the current backbuffer receives N-1.
        if (completed.hasResult && completed.completedFenceValue != 0) {
            if (outputMutex_->AcquireSync(0, 0) != S_OK) return;
            if (FAILED(context4_->Wait(outputFence_.Get(), completed.completedFenceValue))) {
                outputMutex_->ReleaseSync(0);
                InvalidateLocked();
                return;
            }
            if (width_ == nativeWidth_ && height_ == nativeHeight_ && format_ == nativeFormat_) {
                context_->CopyResource(backBuffer.Get(), sharedOutput_.Get());
            } else {
                RecordComposeResidualLocked(backBuffer.Get());
            }
            outputMutex_->ReleaseSync(0);
        }
    }

    void OnResize(IDXGISwapChain* swapChain) {
        std::scoped_lock lock(mutex_);
        if (swapChain_ == swapChain) InvalidateLocked();
    }

    // Tracks whatever the game currently has bound as its output-merger targets. This is a
    // best-effort heuristic for "the main depth buffer": whichever depth-stencil view is still
    // bound when Present is hooked, filtered by resolution in EnsureDepthResourcesLocked. Games
    // that clear their depth binding before Present (rare) or that only ever render depth through
    // a separate pass with no matching resolution will simply keep sending the zero-guide depth
    // the Host already falls back to.
    void OnSetRenderTargets(UINT numViews, ID3D11RenderTargetView* const* renderTargetViews,
                            ID3D11DepthStencilView* depthStencilView) {
        std::scoped_lock lock(mutex_);
        lastRenderTargets_.clear();
        lastRenderTargets_.reserve(numViews);
        for (UINT i = 0; i < numViews; ++i) {
            lastRenderTargets_.emplace_back(renderTargetViews ? renderTargetViews[i] : nullptr);
        }
        lastDepthStencilView_ = depthStencilView;
    }

    void Shutdown() {
        std::scoped_lock lock(mutex_);
        InvalidateLocked();
    }

    bool GetTransportInfo(CaptureD3D11TransportInfo& outInfo) {
        std::scoped_lock lock(mutex_);
        outInfo.configured = configured_;
        outInfo.nativeWidth = nativeWidth_;
        outInfo.nativeHeight = nativeHeight_;
        outInfo.workWidth = width_;
        outInfo.workHeight = height_;
        outInfo.nativeFormat = static_cast<uint32_t>(nativeFormat_);
        outInfo.transportFormat = static_cast<uint32_t>(format_);
        outInfo.downsampleDispatches = downsampleDispatches_;
        outInfo.processingMode = processingMode_;
        outInfo.composeDispatches = composeDispatches_;
        outInfo.hasDepthGuide = depthAvailable_;
        outInfo.depthCaptureDispatches = depthCaptureDispatches_;
        return configured_;
    }

private:
    static bool IsHdrFormat(DXGI_FORMAT format) {
        return format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_R10G10B10A2_UNORM;
    }

    bool CreateSharedTextureLocked(const D3D11_TEXTURE2D_DESC& resourceDesc,
                                   bool allowUnorderedAccess,
                                   ComPtr<ID3D11Texture2D>& texture,
                                   ComPtr<IDXGIKeyedMutex>& keyedMutex,
                                   HANDLE& sharedHandle) {
        D3D11_TEXTURE2D_DESC sharedDesc{};
        sharedDesc.Width = resourceDesc.Width;
        sharedDesc.Height = resourceDesc.Height;
        sharedDesc.MipLevels = 1;
        sharedDesc.ArraySize = 1;
        sharedDesc.Format = resourceDesc.Format;
        sharedDesc.SampleDesc.Count = 1;
        sharedDesc.SampleDesc.Quality = 0;
        sharedDesc.Usage = D3D11_USAGE_DEFAULT;
        sharedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET |
                               (allowUnorderedAccess ? D3D11_BIND_UNORDERED_ACCESS : 0u);
        sharedDesc.CPUAccessFlags = 0;
        // The existing D3D11 bridge uses this exact resource contract. The keyed-mutex flag is
        // required by D3D11 for this NT-handle texture shape; synchronization remains on shared
        // D3D11/D3D12 fences, never on CPU AcquireSync/ReleaseSync.
        sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                               D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        if (FAILED(device_->CreateTexture2D(&sharedDesc, nullptr, &texture))) return false;
        ComPtr<IDXGIResource1> resource;
        if (FAILED(texture.As(&resource)) || FAILED(texture.As(&keyedMutex))) return false;
        return SUCCEEDED(resource->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &sharedHandle));
    }

    bool CreateScalingResourcesLocked(const D3D11_TEXTURE2D_DESC& backBufferDesc, DXGI_FORMAT transportFormat) {
        D3D11_TEXTURE2D_DESC sourceDesc = backBufferDesc;
        sourceDesc.MipLevels = 1;
        sourceDesc.ArraySize = 1;
        sourceDesc.SampleDesc.Count = 1;
        sourceDesc.SampleDesc.Quality = 0;
        sourceDesc.Usage = D3D11_USAGE_DEFAULT;
        sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        sourceDesc.CPUAccessFlags = 0;
        sourceDesc.MiscFlags = 0;
        if (FAILED(device_->CreateTexture2D(&sourceDesc, nullptr, &downsampleSource_))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sourceViewDesc{};
        sourceViewDesc.Format = backBufferDesc.Format;
        sourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sourceViewDesc.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(downsampleSource_.Get(), &sourceViewDesc,
                                                     &downsampleSourceView_))) return false;

        const char* shaderSource = MatchedResidualShaderSource();
        const UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;

        ComPtr<ID3DBlob> downBlob;
        ComPtr<ID3DBlob> downErr;
        if (FAILED(D3DCompile(shaderSource, std::strlen(shaderSource), "MatchedResidual.hlsl", nullptr, nullptr,
                              "CSDownsample", "cs_5_0", compileFlags, 0, &downBlob, &downErr)) ||
            FAILED(device_->CreateComputeShader(downBlob->GetBufferPointer(), downBlob->GetBufferSize(),
                                                nullptr, &downsampleShader_))) {
            OutputDebugStringA("NRFusion Capture32: D3D11 CSDownsample compilation/creation failed.\n");
            return false;
        }

        D3D11_BUFFER_DESC downConstantDesc{};
        downConstantDesc.ByteWidth = 32;
        downConstantDesc.Usage = D3D11_USAGE_DEFAULT;
        downConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&downConstantDesc, nullptr, &downsampleConstants_))) return false;

        ComPtr<ID3DBlob> compBlob;
        ComPtr<ID3DBlob> compErr;
        if (FAILED(D3DCompile(shaderSource, std::strlen(shaderSource), "MatchedResidual.hlsl", nullptr, nullptr,
                              "CSComposeResidual", "cs_5_0", compileFlags, 0, &compBlob, &compErr)) ||
            FAILED(device_->CreateComputeShader(compBlob->GetBufferPointer(), compBlob->GetBufferSize(),
                                                nullptr, &composeShader_))) {
            OutputDebugStringA("NRFusion Capture32: D3D11 CSComposeResidual compilation/creation failed.\n");
            return false;
        }

        D3D11_BUFFER_DESC compConstantDesc{};
        compConstantDesc.ByteWidth = 32;
        compConstantDesc.Usage = D3D11_USAGE_DEFAULT;
        compConstantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&compConstantDesc, nullptr, &composeConstants_))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC outputViewDesc{};
        outputViewDesc.Format = transportFormat;
        outputViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        outputViewDesc.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(sharedOutput_.Get(), &outputViewDesc,
                                                     &sharedOutputView_))) return false;

        D3D11_TEXTURE2D_DESC targetDesc = backBufferDesc;
        targetDesc.MipLevels = 1;
        targetDesc.ArraySize = 1;
        targetDesc.SampleDesc.Count = 1;
        targetDesc.SampleDesc.Quality = 0;
        targetDesc.Usage = D3D11_USAGE_DEFAULT;
        targetDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        targetDesc.CPUAccessFlags = 0;
        targetDesc.MiscFlags = 0;
        if (FAILED(device_->CreateTexture2D(&targetDesc, nullptr, &composeTarget_))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC targetUavDesc{};
        targetUavDesc.Format = backBufferDesc.Format;
        targetUavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        targetUavDesc.Texture2D.MipSlice = 0;
        return SUCCEEDED(device_->CreateUnorderedAccessView(composeTarget_.Get(), &targetUavDesc, &composeTargetUav_));
    }

    bool RecordDownsampleLocked(ID3D11Texture2D* backBuffer) {
        if (!downsampleSource_ || !downsampleSourceView_ || !downsampleShader_ ||
            !downsampleConstants_ || !sharedColorUav_) return false;

        context_->CopyResource(downsampleSource_.Get(), backBuffer);
        struct DownsampleConstants {
            uint32_t sourceWidth;
            uint32_t sourceHeight;
            uint32_t targetWidth;
            uint32_t targetHeight;
            float padding[4];
        } constants{nativeWidth_, nativeHeight_, width_, height_, {0.0f, 0.0f, 0.0f, 0.0f}};
        context_->UpdateSubresource(downsampleConstants_.Get(), 0, nullptr, &constants, 0, 0);

        ID3D11ShaderResourceView* sourceView = downsampleSourceView_.Get();
        ID3D11UnorderedAccessView* targetView = sharedColorUav_.Get();
        ID3D11Buffer* constantBuffer = downsampleConstants_.Get();
        context_->CSSetShader(downsampleShader_.Get(), nullptr, 0);
        context_->CSSetConstantBuffers(0, 1, &constantBuffer);
        context_->CSSetShaderResources(0, 1, &sourceView);
        context_->CSSetUnorderedAccessViews(0, 1, &targetView, nullptr);
        context_->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);

        ID3D11ShaderResourceView* nullSource = nullptr;
        ID3D11UnorderedAccessView* nullTarget = nullptr;
        context_->CSSetShaderResources(0, 1, &nullSource);
        context_->CSSetUnorderedAccessViews(0, 1, &nullTarget, nullptr);
        ID3D11Buffer* nullConstantBuffer = nullptr;
        context_->CSSetConstantBuffers(0, 1, &nullConstantBuffer);
        context_->CSSetShader(nullptr, nullptr, 0);
        ++downsampleDispatches_;
        return true;
    }

    bool RecordComposeResidualLocked(ID3D11Texture2D* backBuffer) {
        if (!composeShader_ || !composeConstants_ || !downsampleSourceView_ ||
            !sharedOutputView_ || !composeTargetUav_ || !composeTarget_) return false;

        const bool isNeural = processingMode_ == static_cast<uint32_t>(IpcProcessingMode::Neural);
        struct ComposeConstants {
            uint32_t nativeWidth;
            uint32_t nativeHeight;
            uint32_t residualWidth;
            uint32_t residualHeight;
            float residualWeight;
            float composeExposure;
            float pad0;
            float pad1;
        } constants{
            nativeWidth_,
            nativeHeight_,
            width_,
            height_,
            1.0f,
            isNeural ? 1.0f : 0.0f,
            0.0f,
            0.0f
        };
        context_->UpdateSubresource(composeConstants_.Get(), 0, nullptr, &constants, 0, 0);

        ID3D11ShaderResourceView* srvs[2] = { downsampleSourceView_.Get(), sharedOutputView_.Get() };
        ID3D11UnorderedAccessView* uavs[1] = { composeTargetUav_.Get() };
        ID3D11Buffer* constantBuffer = composeConstants_.Get();

        context_->CSSetShader(composeShader_.Get(), nullptr, 0);
        context_->CSSetConstantBuffers(0, 1, &constantBuffer);
        context_->CSSetShaderResources(0, 2, srvs);
        context_->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        context_->Dispatch((nativeWidth_ + 15) / 16, (nativeHeight_ + 15) / 16, 1);

        ID3D11ShaderResourceView* nullSrvs[2] = { nullptr, nullptr };
        ID3D11UnorderedAccessView* nullUavs[1] = { nullptr };
        ID3D11Buffer* nullBuffer = nullptr;
        context_->CSSetShaderResources(0, 2, nullSrvs);
        context_->CSSetUnorderedAccessViews(0, 1, nullUavs, nullptr);
        context_->CSSetConstantBuffers(0, 1, &nullBuffer);
        context_->CSSetShader(nullptr, nullptr, 0);

        context_->CopyResource(backBuffer, composeTarget_.Get());
        ++composeDispatches_;
        return true;
    }

    static bool MapDepthSrvFormatLocked(DXGI_FORMAT resourceFormat, DXGI_FORMAT& srvFormat) {
        switch (resourceFormat) {
            case DXGI_FORMAT_R32_TYPELESS:
                srvFormat = DXGI_FORMAT_R32_FLOAT;
                return true;
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
                return true;
            case DXGI_FORMAT_R24G8_TYPELESS:
                srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                return true;
            case DXGI_FORMAT_R16_TYPELESS:
                srvFormat = DXGI_FORMAT_R16_UNORM;
                return true;
            default:
                // A depth-stencil view created directly over a typed format (D32_FLOAT, D24S8,
                // D16_UNORM) cannot be reinterpreted as shader-visible in D3D11 - only the
                // *_TYPELESS resource shapes above can. Bail out rather than guess.
                return false;
        }
    }

    // Builds (once) the shared R16F transport texture and compute pipeline that turns the game's
    // currently bound depth-stencil resource into the guide format HostServer64/DLSS-NR already
    // expects (see lowGuideDepth_ on the host side - same R16G16B16A16_FLOAT shape, depth in .r).
    bool EnsureDepthResourcesLocked() {
        depthSourceView_.Reset();
        if (!lastDepthStencilView_) return false;

        ComPtr<ID3D11Resource> depthResource;
        lastDepthStencilView_->GetResource(&depthResource);
        ComPtr<ID3D11Texture2D> depthTexture;
        if (!depthResource || FAILED(depthResource.As(&depthTexture))) return false;

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthTexture->GetDesc(&depthDesc);
        if (depthDesc.ArraySize != 1 || depthDesc.SampleDesc.Count != 1) return false;

        // Heuristic: only trust a depth buffer close to the backbuffer's own resolution. This
        // filters out shadow maps and other small auxiliary depth targets that also flow through
        // OMSetRenderTargets but are not the main scene depth.
        if (depthDesc.Width < nativeWidth_ / 2 || depthDesc.Height < nativeHeight_ / 2) return false;

        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (!MapDepthSrvFormatLocked(depthDesc.Format, srvFormat)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = srvFormat;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(depthTexture.Get(), &srvDesc, &depthSourceView_))) {
            return false;
        }
        depthSourceWidth_ = depthDesc.Width;
        depthSourceHeight_ = depthDesc.Height;

        if (!sharedDepth_) {
            D3D11_TEXTURE2D_DESC transportDesc{};
            transportDesc.Width = width_;
            transportDesc.Height = height_;
            transportDesc.MipLevels = 1;
            transportDesc.ArraySize = 1;
            transportDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            transportDesc.SampleDesc.Count = 1;
            transportDesc.SampleDesc.Quality = 0;
            if (!CreateSharedTextureLocked(transportDesc, /*allowUnorderedAccess=*/true, sharedDepth_,
                                           depthMutex_, depthHandle_)) {
                sharedDepth_.Reset();
                return false;
            }
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
            if (FAILED(device_->CreateUnorderedAccessView(sharedDepth_.Get(), &uavDesc, &depthTargetUav_))) {
                sharedDepth_.Reset();
                return false;
            }
        }

        if (!depthCaptureShader_) {
            const char* shaderSource = MatchedResidualShaderSource();
            const UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
            ComPtr<ID3DBlob> blob;
            ComPtr<ID3DBlob> err;
            if (FAILED(D3DCompile(shaderSource, std::strlen(shaderSource), "MatchedResidual.hlsl", nullptr,
                                  nullptr, "CSCaptureDepth", "cs_5_0", compileFlags, 0, &blob, &err)) ||
                FAILED(device_->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr,
                                                    &depthCaptureShader_))) {
                OutputDebugStringA("NRFusion Capture32: D3D11 CSCaptureDepth compilation/creation failed.\n");
                return false;
            }
            D3D11_BUFFER_DESC constantDesc{};
            constantDesc.ByteWidth = 32;
            constantDesc.Usage = D3D11_USAGE_DEFAULT;
            constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (FAILED(device_->CreateBuffer(&constantDesc, nullptr, &depthCaptureConstants_))) return false;
        }

        return true;
    }

    // Reads the depth resource while it is still bound to the output-merger (typical at Present
    // time), so the D3D11 resource-hazard check would otherwise silently unbind it. Detach and
    // restore the exact OM state the game had, using the untouched OMSetRenderTargets pointer
    // directly - going through our own hook here would both re-enter this class's mutex_ (already
    // held by the caller) and pollute lastRenderTargets_/lastDepthStencilView_ with our own
    // temporary unbind.
    bool RecordCaptureDepthLocked() {
        if (!depthCaptureShader_ || !depthCaptureConstants_ || !depthSourceView_ || !depthTargetUav_) return false;

        const OMSetRenderTargetsFn original = OriginalOMSetRenderTargets(context_.Get());
        if (!original) return false;

        std::vector<ComPtr<ID3D11RenderTargetView>> savedRtvs = lastRenderTargets_;
        ComPtr<ID3D11DepthStencilView> savedDsv = lastDepthStencilView_;
        original(context_.Get(), 0, nullptr, nullptr);

        struct DepthCaptureConstants {
            uint32_t srcWidth;
            uint32_t srcHeight;
            uint32_t dstWidth;
            uint32_t dstHeight;
            float padding[4];
        } constants{depthSourceWidth_, depthSourceHeight_, width_, height_, {0.0f, 0.0f, 0.0f, 0.0f}};
        context_->UpdateSubresource(depthCaptureConstants_.Get(), 0, nullptr, &constants, 0, 0);

        ID3D11ShaderResourceView* sourceView = depthSourceView_.Get();
        ID3D11UnorderedAccessView* targetView = depthTargetUav_.Get();
        ID3D11Buffer* constantBuffer = depthCaptureConstants_.Get();
        context_->CSSetShader(depthCaptureShader_.Get(), nullptr, 0);
        context_->CSSetConstantBuffers(0, 1, &constantBuffer);
        context_->CSSetShaderResources(0, 1, &sourceView);
        context_->CSSetUnorderedAccessViews(0, 1, &targetView, nullptr);
        context_->Dispatch((width_ + 15) / 16, (height_ + 15) / 16, 1);

        ID3D11ShaderResourceView* nullSource = nullptr;
        ID3D11UnorderedAccessView* nullTarget = nullptr;
        context_->CSSetShaderResources(0, 1, &nullSource);
        context_->CSSetUnorderedAccessViews(0, 1, &nullTarget, nullptr);
        ID3D11Buffer* nullConstantBuffer = nullptr;
        context_->CSSetConstantBuffers(0, 1, &nullConstantBuffer);
        context_->CSSetShader(nullptr, nullptr, 0);

        std::vector<ID3D11RenderTargetView*> rawRtvs;
        rawRtvs.reserve(savedRtvs.size());
        for (auto& rtv : savedRtvs) rawRtvs.push_back(rtv.Get());
        original(context_.Get(), static_cast<UINT>(rawRtvs.size()), rawRtvs.empty() ? nullptr : rawRtvs.data(),
                savedDsv.Get());

        ++depthCaptureDispatches_;
        return true;
    }

    bool CreateSharedFencesLocked() {
        if (FAILED(device5_->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&inputFence_))) ||
            FAILED(device5_->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&outputFence_))) ||
            FAILED(inputFence_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &inputFenceHandle_)) ||
            FAILED(outputFence_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &outputFenceHandle_))) {
            return false;
        }
        return true;
    }

    bool EnsureTransportLocked(IDXGISwapChain* swapChain, ID3D11Texture2D* backBuffer) {
        D3D11_TEXTURE2D_DESC backBufferDesc{};
        backBuffer->GetDesc(&backBufferDesc);
        if (backBufferDesc.Width == 0 || backBufferDesc.Height == 0 ||
            backBufferDesc.Format == DXGI_FORMAT_UNKNOWN || backBufferDesc.SampleDesc.Count != 1) {
            OutputDebugStringA("NRFusion Capture32: D3D11 backbuffer is not a single-sample shareable texture.\n");
            return false;
        }

        const bool reduced = workingScale_ < 0.999f;
        const bool isNeural = processingMode_ == static_cast<uint32_t>(IpcProcessingMode::Neural);
        const uint32_t workWidth = reduced
            ? std::max(1u, static_cast<uint32_t>(std::lround(backBufferDesc.Width * workingScale_)))
            : backBufferDesc.Width;
        const uint32_t workHeight = reduced
            ? std::max(1u, static_cast<uint32_t>(std::lround(backBufferDesc.Height * workingScale_)))
            : backBufferDesc.Height;
        const bool needsConversion = reduced || (isNeural && backBufferDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT);
        const DXGI_FORMAT transportFormat = (reduced || isNeural) ? DXGI_FORMAT_R16G16B16A16_FLOAT : backBufferDesc.Format;

        if (configured_ && swapChain_ == swapChain && width_ == workWidth && height_ == workHeight &&
            format_ == transportFormat && nativeWidth_ == backBufferDesc.Width &&
            nativeHeight_ == backBufferDesc.Height && nativeFormat_ == backBufferDesc.Format) {
            return true;
        }

        InvalidateLocked();
        swapChain_ = swapChain;
        backBuffer->GetDevice(device_.ReleaseAndGetAddressOf());
        if (!device_) return false;
        device_->GetImmediateContext(&context_);
        if (!context_ || FAILED(device_.As(&device5_)) || FAILED(context_.As(&context4_))) {
            OutputDebugStringA("NRFusion Capture32: shared D3D11 fences require ID3D11Device5 and ID3D11DeviceContext4.\n");
            InvalidateLocked();
            return false;
        }

        nativeWidth_ = backBufferDesc.Width;
        nativeHeight_ = backBufferDesc.Height;
        nativeFormat_ = backBufferDesc.Format;
        width_ = workWidth;
        height_ = workHeight;
        format_ = transportFormat;

        D3D11_TEXTURE2D_DESC transportDesc = backBufferDesc;
        transportDesc.Width = workWidth;
        transportDesc.Height = workHeight;
        transportDesc.Format = transportFormat;
        transportDesc.MipLevels = 1;
        transportDesc.ArraySize = 1;
        transportDesc.SampleDesc.Count = 1;
        transportDesc.SampleDesc.Quality = 0;

        const bool colorUav = needsConversion || isNeural;
        const bool outputUav = isNeural;
        if (!CreateSharedTextureLocked(transportDesc, colorUav, sharedColor_, colorMutex_, colorHandle_) ||
            !CreateSharedTextureLocked(transportDesc, outputUav, sharedOutput_, outputMutex_, outputHandle_)) {
            OutputDebugStringA("NRFusion Capture32: failed to create shared Color/Output resources or shared fences.\n");
            InvalidateLocked();
            return false;
        }

        if (needsConversion) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = transportFormat;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
            if (FAILED(device_->CreateUnorderedAccessView(sharedColor_.Get(), &uavDesc, &sharedColorUav_)) ||
                !CreateScalingResourcesLocked(backBufferDesc, transportFormat)) {
                OutputDebugStringA("NRFusion Capture32: failed to create D3D11 scaling resources.\n");
                InvalidateLocked();
                return false;
            }
        }
        if (!CreateSharedFencesLocked()) {
            OutputDebugStringA("NRFusion Capture32: failed to create shared D3D11 fences.\n");
            InvalidateLocked();
            return false;
        }

        depthAvailable_ = EnsureDepthResourcesLocked();

        if (!NRFusion_Capture32_Connect(0, 3000)) {
            OutputDebugStringA("NRFusion Capture32: HostServer64 connection failed.\n");
            InvalidateLocked();
            return false;
        }

        CaptureClientConfig config{};
        config.width = width_;
        config.height = height_;
        config.targetWidth = nativeWidth_;
        config.targetHeight = nativeHeight_;
        config.workingScale = workingScale_;
        config.isHdr = IsHdrFormat(format_);
        config.depthInverted = true;
        config.colorFormat = static_cast<uint32_t>(format_);
        config.processingMode = static_cast<IpcProcessingMode>(processingMode_);
        config.colorSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(colorHandle_));
        config.residualSharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(outputHandle_));
        config.producerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(inputFenceHandle_));
        config.consumerFenceHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(outputFenceHandle_));
        config.depthSharedHandle = depthAvailable_
            ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(depthHandle_)) : 0;

        if (!NRFusion_Capture32_Configure(&config)) {
            OutputDebugStringA("NRFusion Capture32: HostServer64 rejected shared transport configuration.\n");
            InvalidateLocked();
            return false;
        }

        configured_ = true;
        return true;
    }

    void InvalidateLocked() {
        if (configured_ || NRFusion_Capture32_IsConnected()) NRFusion_Capture32_Disconnect();
        configured_ = false;

        if (colorHandle_) CloseHandle(colorHandle_);
        if (outputHandle_) CloseHandle(outputHandle_);
        if (inputFenceHandle_) CloseHandle(inputFenceHandle_);
        if (outputFenceHandle_) CloseHandle(outputFenceHandle_);
        colorHandle_ = nullptr;
        outputHandle_ = nullptr;
        inputFenceHandle_ = nullptr;
        outputFenceHandle_ = nullptr;

        sharedColor_.Reset();
        sharedOutput_.Reset();
        sharedColorUav_.Reset();
        downsampleSource_.Reset();
        downsampleSourceView_.Reset();
        downsampleShader_.Reset();
        downsampleConstants_.Reset();
        colorMutex_.Reset();
        outputMutex_.Reset();
        inputFence_.Reset();
        outputFence_.Reset();
        context4_.Reset();
        device5_.Reset();
        context_.Reset();
        device_.Reset();
        swapChain_ = nullptr;
        width_ = 0;
        height_ = 0;
        nativeWidth_ = 0;
        nativeHeight_ = 0;
        format_ = DXGI_FORMAT_UNKNOWN;
        nativeFormat_ = DXGI_FORMAT_UNKNOWN;
        downsampleDispatches_ = 0;
        sharedOutputView_.Reset();
        composeShader_.Reset();
        composeConstants_.Reset();
        composeTarget_.Reset();
        composeTargetUav_.Reset();
        composeDispatches_ = 0;

        if (depthHandle_) CloseHandle(depthHandle_);
        depthHandle_ = nullptr;
        sharedDepth_.Reset();
        depthMutex_.Reset();
        depthTargetUav_.Reset();
        depthSourceView_.Reset();
        depthCaptureShader_.Reset();
        depthCaptureConstants_.Reset();
        depthAvailable_ = false;
        depthCaptureDispatches_ = 0;
        depthSourceWidth_ = 0;
        depthSourceHeight_ = 0;
    }

    std::mutex mutex_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Device5> device5_;
    ComPtr<ID3D11DeviceContext4> context4_;
    ComPtr<ID3D11Texture2D> sharedColor_;
    ComPtr<ID3D11Texture2D> sharedOutput_;
    ComPtr<ID3D11UnorderedAccessView> sharedColorUav_;
    ComPtr<ID3D11Texture2D> downsampleSource_;
    ComPtr<ID3D11ShaderResourceView> downsampleSourceView_;
    ComPtr<ID3D11ComputeShader> downsampleShader_;
    ComPtr<ID3D11Buffer> downsampleConstants_;
    ComPtr<ID3D11ShaderResourceView> sharedOutputView_;
    ComPtr<ID3D11ComputeShader> composeShader_;
    ComPtr<ID3D11Buffer> composeConstants_;
    ComPtr<ID3D11Texture2D> composeTarget_;
    ComPtr<ID3D11UnorderedAccessView> composeTargetUav_;
    ComPtr<IDXGIKeyedMutex> colorMutex_;
    ComPtr<IDXGIKeyedMutex> outputMutex_;
    ComPtr<ID3D11Fence> inputFence_;
    ComPtr<ID3D11Fence> outputFence_;
    IDXGISwapChain* swapChain_ = nullptr;
    HANDLE colorHandle_ = nullptr;
    HANDLE outputHandle_ = nullptr;
    HANDLE inputFenceHandle_ = nullptr;
    HANDLE outputFenceHandle_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t nativeWidth_ = 0;
    uint32_t nativeHeight_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT nativeFormat_ = DXGI_FORMAT_UNKNOWN;
    float workingScale_ = 1.0f;
    uint32_t processingMode_ = static_cast<uint32_t>(IpcProcessingMode::DummyCopy);
    uint64_t downsampleDispatches_ = 0;
    uint64_t composeDispatches_ = 0;
    uint64_t nextWorkId_ = 1;
    uint64_t nextInputFenceValue_ = 1;
    uint64_t nextOutputFenceValue_ = 1;
    bool configured_ = false;

    // Output-merger tracking (game state - survives InvalidateLocked, unlike everything below).
    std::vector<ComPtr<ID3D11RenderTargetView>> lastRenderTargets_;
    ComPtr<ID3D11DepthStencilView> lastDepthStencilView_;

    // Depth guide transport (session state - reset by InvalidateLocked like the color path).
    ComPtr<ID3D11ShaderResourceView> depthSourceView_;
    ComPtr<ID3D11Texture2D> sharedDepth_;
    ComPtr<IDXGIKeyedMutex> depthMutex_;
    ComPtr<ID3D11UnorderedAccessView> depthTargetUav_;
    ComPtr<ID3D11ComputeShader> depthCaptureShader_;
    ComPtr<ID3D11Buffer> depthCaptureConstants_;
    HANDLE depthHandle_ = nullptr;
    uint32_t depthSourceWidth_ = 0;
    uint32_t depthSourceHeight_ = 0;
    bool depthAvailable_ = false;
    uint64_t depthCaptureDispatches_ = 0;
};

std::atomic<bool> g_stopRequested{false};
std::atomic<bool> g_hookWorkerStarted{false};
std::atomic<CreateDeviceAndSwapChainFn> g_originalCreateDeviceAndSwapChain{nullptr};
std::atomic<CreateDxgiFactoryFn> g_originalCreateDXGIFactory{nullptr};
std::atomic<CreateDxgiFactoryFn> g_originalCreateDXGIFactory1{nullptr};
std::atomic<CreateDxgiFactory2Fn> g_originalCreateDXGIFactory2{nullptr};
std::mutex g_hookMutex;
std::unordered_map<void**, VtableOriginals> g_swapChainVtables;
std::unordered_map<void**, ContextVtableOriginals> g_contextVtables;
std::unordered_map<void**, FactoryVtableOriginals> g_factoryVtables;
std::vector<IatPatch> g_iatPatches;
D3D11CaptureSession g_captureSession;

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                               UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags);
void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* context, UINT numViews,
                                                 ID3D11RenderTargetView* const* renderTargetViews,
                                                 ID3D11DepthStencilView* depthStencilView);
HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChain(IDXGIFactory* factory, IUnknown* device,
                                                        DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain);
HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
                                                               const DXGI_SWAP_CHAIN_DESC1* desc,
                                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                               IDXGIOutput* restrictToOutput,
                                                               IDXGISwapChain1** swapChain);

bool ReplacePointer(void** slot, void* replacement, void*& previous) {
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) return false;
    previous = InterlockedExchangePointer(reinterpret_cast<PVOID*>(slot), replacement);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return true;
}

void RestorePointer(void** slot, void* original) {
    if (!slot || !original) return;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtection)) return;
    InterlockedExchangePointer(reinterpret_cast<PVOID*>(slot), original);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
}

bool PatchSwapChainVtable(IDXGISwapChain* swapChain) {
    if (!swapChain) return false;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    if (!vtable) return false;

    std::scoped_lock lock(g_hookMutex);
    if (g_stopRequested || g_swapChainVtables.contains(vtable)) return !g_stopRequested;

    VtableOriginals originals{
        reinterpret_cast<PresentFn>(vtable[kSwapChainPresentIndex]),
        reinterpret_cast<ResizeBuffersFn>(vtable[kSwapChainResizeBuffersIndex]),
    };
    if (!originals.present || !originals.resizeBuffers) return false;

    g_swapChainVtables.emplace(vtable, originals);
    void* previousPresent = nullptr;
    void* previousResize = nullptr;
    if (!ReplacePointer(&vtable[kSwapChainPresentIndex], reinterpret_cast<void*>(&HookedPresent), previousPresent) ||
        !ReplacePointer(&vtable[kSwapChainResizeBuffersIndex], reinterpret_cast<void*>(&HookedResizeBuffers), previousResize)) {
        RestorePointer(&vtable[kSwapChainPresentIndex], reinterpret_cast<void*>(originals.present));
        RestorePointer(&vtable[kSwapChainResizeBuffersIndex], reinterpret_cast<void*>(originals.resizeBuffers));
        g_swapChainVtables.erase(vtable);
        return false;
    }
    return true;
}

bool PatchContextVtable(ID3D11DeviceContext* context) {
    if (!context) return false;
    void** vtable = *reinterpret_cast<void***>(context);
    if (!vtable) return false;

    std::scoped_lock lock(g_hookMutex);
    if (g_stopRequested || g_contextVtables.contains(vtable)) return !g_stopRequested;

    ContextVtableOriginals originals{
        reinterpret_cast<OMSetRenderTargetsFn>(vtable[kContextOMSetRenderTargetsIndex]),
    };
    if (!originals.omSetRenderTargets) return false;

    g_contextVtables.emplace(vtable, originals);
    void* previous = nullptr;
    if (!ReplacePointer(&vtable[kContextOMSetRenderTargetsIndex], reinterpret_cast<void*>(&HookedOMSetRenderTargets),
                        previous)) {
        g_contextVtables.erase(vtable);
        return false;
    }
    return true;
}

// factory may be any IDXGIFactory (or later revision); CreateSwapChainForHwnd's slot only exists
// once the object actually implements IDXGIFactory2, so that slot is patched only after a
// QueryInterface confirms it - writing slot 15 on a bare IDXGIFactory vtable would be OOB.
bool PatchFactoryVtable(IDXGIFactory* factory) {
    if (!factory) return false;
    void** vtable = *reinterpret_cast<void***>(factory);
    if (!vtable) return false;

    std::scoped_lock lock(g_hookMutex);
    if (g_stopRequested || g_factoryVtables.contains(vtable)) return !g_stopRequested;

    FactoryVtableOriginals originals{
        reinterpret_cast<DxgiCreateSwapChainFn>(vtable[kFactoryCreateSwapChainIndex]),
        nullptr,
    };
    if (!originals.createSwapChain) return false;

    void* previousCreateSwapChain = nullptr;
    if (!ReplacePointer(&vtable[kFactoryCreateSwapChainIndex],
                        reinterpret_cast<void*>(&HookedFactoryCreateSwapChain), previousCreateSwapChain)) {
        return false;
    }

    ComPtr<IDXGIFactory2> factory2;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory2))) && factory2) {
        originals.createSwapChainForHwnd =
            reinterpret_cast<DxgiCreateSwapChainForHwndFn>(vtable[kFactory2CreateSwapChainForHwndIndex]);
        void* previousCreateSwapChainForHwnd = nullptr;
        if (originals.createSwapChainForHwnd &&
            !ReplacePointer(&vtable[kFactory2CreateSwapChainForHwndIndex],
                            reinterpret_cast<void*>(&HookedFactoryCreateSwapChainForHwnd),
                            previousCreateSwapChainForHwnd)) {
            originals.createSwapChainForHwnd = nullptr;
        }
    }

    g_factoryVtables.emplace(vtable, originals);
    return true;
}

PresentFn OriginalPresent(IDXGISwapChain* swapChain) {
    if (!swapChain) return nullptr;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_swapChainVtables.find(vtable);
    return found == g_swapChainVtables.end() ? nullptr : found->second.present;
}

ResizeBuffersFn OriginalResizeBuffers(IDXGISwapChain* swapChain) {
    if (!swapChain) return nullptr;
    void** vtable = *reinterpret_cast<void***>(swapChain);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_swapChainVtables.find(vtable);
    return found == g_swapChainVtables.end() ? nullptr : found->second.resizeBuffers;
}

OMSetRenderTargetsFn OriginalOMSetRenderTargets(ID3D11DeviceContext* context) {
    if (!context) return nullptr;
    void** vtable = *reinterpret_cast<void***>(context);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_contextVtables.find(vtable);
    return found == g_contextVtables.end() ? nullptr : found->second.omSetRenderTargets;
}

DxgiCreateSwapChainFn OriginalFactoryCreateSwapChain(IDXGIFactory* factory) {
    if (!factory) return nullptr;
    void** vtable = *reinterpret_cast<void***>(factory);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_factoryVtables.find(vtable);
    return found == g_factoryVtables.end() ? nullptr : found->second.createSwapChain;
}

DxgiCreateSwapChainForHwndFn OriginalFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory) {
    if (!factory) return nullptr;
    void** vtable = *reinterpret_cast<void***>(factory);
    std::scoped_lock lock(g_hookMutex);
    const auto found = g_factoryVtables.find(vtable);
    return found == g_factoryVtables.end() ? nullptr : found->second.createSwapChainForHwnd;
}

// Resolves the ID3D11Device/ID3D11DeviceContext behind the IUnknown* that CreateSwapChain and
// CreateSwapChainForHwnd receive (the app's already-created device, since the split path never
// funnels device creation through the swap-chain call), then attaches the capture session exactly
// as the monolithic D3D11CreateDeviceAndSwapChain path does.
void AttachFromDeviceUnknown(IUnknown* deviceUnknown, IDXGISwapChain* swapChain) {
    if (!deviceUnknown || !swapChain) return;
    ComPtr<ID3D11Device> device;
    if (FAILED(deviceUnknown->QueryInterface(IID_PPV_ARGS(&device))) || !device) return;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);

    PatchSwapChainVtable(swapChain);
    if (context) PatchContextVtable(context.Get());
    g_captureSession.Attach(device.Get(), context.Get(), swapChain);
}

void STDMETHODCALLTYPE HookedOMSetRenderTargets(ID3D11DeviceContext* context, UINT numViews,
                                                 ID3D11RenderTargetView* const* renderTargetViews,
                                                 ID3D11DepthStencilView* depthStencilView) {
    g_captureSession.OnSetRenderTargets(numViews, renderTargetViews, depthStencilView);
    const OMSetRenderTargetsFn original = OriginalOMSetRenderTargets(context);
    if (original) original(context, numViews, renderTargetViews, depthStencilView);
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    const PresentFn original = OriginalPresent(swapChain);
    if (!original) return DXGI_ERROR_INVALID_CALL;
    g_captureSession.OnPresent(swapChain);
    return original(swapChain, syncInterval, flags);
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width,
                                               UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
    const ResizeBuffersFn original = OriginalResizeBuffers(swapChain);
    if (!original) return DXGI_ERROR_INVALID_CALL;
    g_captureSession.OnResize(swapChain);
    return original(swapChain, bufferCount, width, height, newFormat, swapChainFlags);
}

HRESULT WINAPI HookedCreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driverType,
                                              HMODULE software, UINT flags,
                                              const D3D_FEATURE_LEVEL* featureLevels,
                                              UINT featureLevelCount, UINT sdkVersion,
                                              const DXGI_SWAP_CHAIN_DESC* swapChainDesc,
                                              IDXGISwapChain** swapChain, ID3D11Device** device,
                                              D3D_FEATURE_LEVEL* selectedFeatureLevel,
                                              ID3D11DeviceContext** immediateContext) {
    const CreateDeviceAndSwapChainFn original = g_originalCreateDeviceAndSwapChain.load();
    if (!original) return E_FAIL;

    const HRESULT result = original(adapter, driverType, software, flags, featureLevels, featureLevelCount,
                                    sdkVersion, swapChainDesc, swapChain, device, selectedFeatureLevel,
                                    immediateContext);
    if (SUCCEEDED(result) && swapChain && *swapChain) {
        PatchSwapChainVtable(*swapChain);
        if (immediateContext && *immediateContext) PatchContextVtable(*immediateContext);
        g_captureSession.Attach(device ? *device : nullptr, immediateContext ? *immediateContext : nullptr, *swapChain);
    }
    return result;
}

HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChain(IDXGIFactory* factory, IUnknown* device,
                                                        DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain) {
    const DxgiCreateSwapChainFn original = OriginalFactoryCreateSwapChain(factory);
    if (!original) return E_FAIL;
    const HRESULT result = original(factory, device, desc, swapChain);
    if (SUCCEEDED(result) && swapChain && *swapChain) AttachFromDeviceUnknown(device, *swapChain);
    return result;
}

HRESULT STDMETHODCALLTYPE HookedFactoryCreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
                                                               const DXGI_SWAP_CHAIN_DESC1* desc,
                                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
                                                               IDXGIOutput* restrictToOutput,
                                                               IDXGISwapChain1** swapChain) {
    const DxgiCreateSwapChainForHwndFn original = OriginalFactoryCreateSwapChainForHwnd(factory);
    if (!original) return E_FAIL;
    const HRESULT result = original(factory, device, hwnd, desc, fullscreenDesc, restrictToOutput, swapChain);
    if (SUCCEEDED(result) && swapChain && *swapChain) AttachFromDeviceUnknown(device, *swapChain);
    return result;
}

HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** factory);
HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** factory);
HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void** factory);

// One spec per IAT-hookable creation entry point. recordOriginal stashes the resolved original
// into that function's typed atomic so its Hooked* trampoline can call back into the real thing.
struct ImportHookSpec {
    const char* dllName;
    const char* functionName;
    void* hookFunction;
    void (*recordOriginal)(void* original);
};

void RecordOriginalCreateDeviceAndSwapChain(void* original) {
    CreateDeviceAndSwapChainFn expected = nullptr;
    g_originalCreateDeviceAndSwapChain.compare_exchange_strong(
        expected, reinterpret_cast<CreateDeviceAndSwapChainFn>(original));
}

void RecordOriginalCreateDXGIFactory(void* original) {
    CreateDxgiFactoryFn expected = nullptr;
    g_originalCreateDXGIFactory.compare_exchange_strong(expected, reinterpret_cast<CreateDxgiFactoryFn>(original));
}

void RecordOriginalCreateDXGIFactory1(void* original) {
    CreateDxgiFactoryFn expected = nullptr;
    g_originalCreateDXGIFactory1.compare_exchange_strong(expected, reinterpret_cast<CreateDxgiFactoryFn>(original));
}

void RecordOriginalCreateDXGIFactory2(void* original) {
    CreateDxgiFactory2Fn expected = nullptr;
    g_originalCreateDXGIFactory2.compare_exchange_strong(expected, reinterpret_cast<CreateDxgiFactory2Fn>(original));
}

// Walks one module's import table and patches every IAT slot matching any spec's {dllName,
// functionName}, across d3d11.dll (the monolithic path) and dxgi.dll (the factory-based split
// path) in a single pass, so both creation styles are covered by the same retry loop.
bool PatchModuleImports(HMODULE module, const ImportHookSpec* specs, size_t specCount) {
    auto* base = reinterpret_cast<std::byte*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) return false;
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
    bool patched = false;
    for (; descriptor->Name != 0; ++descriptor) {
        const char* importedDll = reinterpret_cast<const char*>(base + descriptor->Name);
        if (descriptor->OriginalFirstThunk == 0) continue;

        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const char* importedName = reinterpret_cast<const char*>(importByName->Name);

            for (size_t i = 0; i != specCount; ++i) {
                const ImportHookSpec& spec = specs[i];
                if (_stricmp(importedDll, spec.dllName) != 0) continue;
                if (std::strcmp(importedName, spec.functionName) != 0) continue;

                auto** slot = reinterpret_cast<void**>(&addresses->u1.Function);
                void* current = *slot;
                if (current == spec.hookFunction) {
                    patched = true;
                    break;
                }

                void* previous = nullptr;
                if (ReplacePointer(slot, spec.hookFunction, previous)) {
                    spec.recordOriginal(previous);
                    g_iatPatches.push_back({slot, previous});
                    patched = true;
                }
                break;
            }
        }
    }
    return patched;
}

void PatchLoadedD3D11Imports() {
    const ImportHookSpec specs[] = {
        {"d3d11.dll", "D3D11CreateDeviceAndSwapChain",
         reinterpret_cast<void*>(&HookedCreateDeviceAndSwapChain), &RecordOriginalCreateDeviceAndSwapChain},
        {"dxgi.dll", "CreateDXGIFactory", reinterpret_cast<void*>(&HookedCreateDXGIFactory),
         &RecordOriginalCreateDXGIFactory},
        {"dxgi.dll", "CreateDXGIFactory1", reinterpret_cast<void*>(&HookedCreateDXGIFactory1),
         &RecordOriginalCreateDXGIFactory1},
        {"dxgi.dll", "CreateDXGIFactory2", reinterpret_cast<void*>(&HookedCreateDXGIFactory2),
         &RecordOriginalCreateDXGIFactory2},
    };

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Module32First(snapshot, &entry)) {
        do {
            if (g_stopRequested) break;
            PatchModuleImports(entry.hModule, specs, std::size(specs));
        } while (Module32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

HRESULT WINAPI HookedCreateDXGIFactory(REFIID riid, void** factory) {
    const CreateDxgiFactoryFn original = g_originalCreateDXGIFactory.load();
    if (!original) return E_FAIL;
    const HRESULT result = original(riid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        PatchFactoryVtable(reinterpret_cast<IDXGIFactory*>(*factory));
    }
    return result;
}

HRESULT WINAPI HookedCreateDXGIFactory1(REFIID riid, void** factory) {
    const CreateDxgiFactoryFn original = g_originalCreateDXGIFactory1.load();
    if (!original) return E_FAIL;
    const HRESULT result = original(riid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        PatchFactoryVtable(reinterpret_cast<IDXGIFactory*>(*factory));
    }
    return result;
}

HRESULT WINAPI HookedCreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    const CreateDxgiFactory2Fn original = g_originalCreateDXGIFactory2.load();
    if (!original) return E_FAIL;
    const HRESULT result = original(flags, riid, factory);
    if (SUCCEEDED(result) && factory && *factory) {
        PatchFactoryVtable(reinterpret_cast<IDXGIFactory*>(*factory));
    }
    return result;
}

DWORD WINAPI CaptureHookWorker(void*) {
    for (unsigned attempt = 0; attempt != 100 && !g_stopRequested; ++attempt) {
        {
            std::scoped_lock lock(g_hookMutex);
            if (!g_stopRequested) PatchLoadedD3D11Imports();
        }
        if (g_originalCreateDeviceAndSwapChain.load() || g_originalCreateDXGIFactory.load() ||
            g_originalCreateDXGIFactory1.load() || g_originalCreateDXGIFactory2.load()) {
            break;
        }
        Sleep(100);
    }
    return 0;
}

} // namespace

void StartCaptureD3D11Hooks() {
    if (sizeof(void*) != 4) return;
    bool expected = false;
    if (!g_hookWorkerStarted.compare_exchange_strong(expected, true)) return;

    g_stopRequested = false;
    HANDLE worker = CreateThread(nullptr, 0, CaptureHookWorker, nullptr, 0, nullptr);
    if (worker) CloseHandle(worker);
}

void SetCaptureD3D11WorkingScale(float workingScale) {
    g_captureSession.SetWorkingScale(workingScale);
}

void SetCaptureD3D11ProcessingMode(uint32_t mode) {
    g_captureSession.SetProcessingMode(mode);
}

bool GetCaptureD3D11TransportInfo(CaptureD3D11TransportInfo& outInfo) {
    return g_captureSession.GetTransportInfo(outInfo);
}

void StopCaptureD3D11Hooks() {
    g_stopRequested = true;
    g_captureSession.Shutdown();
    std::scoped_lock lock(g_hookMutex);

    for (const IatPatch& patch : g_iatPatches) {
        if (!patch.slot) continue;
        const void* current = *patch.slot;
        if (current == reinterpret_cast<void*>(&HookedCreateDeviceAndSwapChain) ||
            current == reinterpret_cast<void*>(&HookedCreateDXGIFactory) ||
            current == reinterpret_cast<void*>(&HookedCreateDXGIFactory1) ||
            current == reinterpret_cast<void*>(&HookedCreateDXGIFactory2)) {
            RestorePointer(patch.slot, patch.original);
        }
    }
    g_iatPatches.clear();

    for (const auto& [vtable, originals] : g_factoryVtables) {
        if (!vtable) continue;
        if (vtable[kFactoryCreateSwapChainIndex] == reinterpret_cast<void*>(&HookedFactoryCreateSwapChain)) {
            RestorePointer(&vtable[kFactoryCreateSwapChainIndex], reinterpret_cast<void*>(originals.createSwapChain));
        }
        if (originals.createSwapChainForHwnd &&
            vtable[kFactory2CreateSwapChainForHwndIndex] ==
                reinterpret_cast<void*>(&HookedFactoryCreateSwapChainForHwnd)) {
            RestorePointer(&vtable[kFactory2CreateSwapChainForHwndIndex],
                          reinterpret_cast<void*>(originals.createSwapChainForHwnd));
        }
    }
    g_factoryVtables.clear();

    for (const auto& [vtable, originals] : g_swapChainVtables) {
        if (!vtable) continue;
        if (vtable[kSwapChainPresentIndex] == reinterpret_cast<void*>(&HookedPresent)) {
            RestorePointer(&vtable[kSwapChainPresentIndex], reinterpret_cast<void*>(originals.present));
        }
        if (vtable[kSwapChainResizeBuffersIndex] == reinterpret_cast<void*>(&HookedResizeBuffers)) {
            RestorePointer(&vtable[kSwapChainResizeBuffersIndex], reinterpret_cast<void*>(originals.resizeBuffers));
        }
    }
    g_swapChainVtables.clear();

    for (const auto& [vtable, originals] : g_contextVtables) {
        if (!vtable) continue;
        if (vtable[kContextOMSetRenderTargetsIndex] == reinterpret_cast<void*>(&HookedOMSetRenderTargets)) {
            RestorePointer(&vtable[kContextOMSetRenderTargetsIndex],
                          reinterpret_cast<void*>(originals.omSetRenderTargets));
        }
    }
    g_contextVtables.clear();
    g_hookWorkerStarted = false;
}

} // namespace nrfusion
