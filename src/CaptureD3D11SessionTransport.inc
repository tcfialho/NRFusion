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

