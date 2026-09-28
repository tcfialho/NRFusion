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

#include "CaptureD3D11SessionResources.inc"
#include "CaptureD3D11SessionDepth.inc"
#include "CaptureD3D11SessionTransport.inc"

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

