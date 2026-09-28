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
        if (!lastDepthStencilView_) return false;

        ComPtr<ID3D11Resource> depthResource;
        lastDepthStencilView_->GetResource(&depthResource);
        ComPtr<ID3D11Texture2D> depthTexture;
        if (!depthResource || FAILED(depthResource.As(&depthTexture)))
            return false;

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthTexture->GetDesc(&depthDesc);
        if (depthDesc.ArraySize != 1 || depthDesc.SampleDesc.Count != 1)
            return false;
        if (depthDesc.Width < nativeWidth_ / 2 ||
            depthDesc.Height < nativeHeight_ / 2)
            return false;

        DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN;
        if (!MapDepthSrvFormatLocked(depthDesc.Format, srvFormat))
            return false;

        ComPtr<ID3D11ShaderResourceView> sourceView;
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = srvFormat;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        if (FAILED(device_->CreateShaderResourceView(
                depthTexture.Get(), &srvDesc, &sourceView)))
            return false;

        ComPtr<ID3D11Texture2D> sharedDepth;
        ComPtr<IDXGIKeyedMutex> depthMutex;
        HANDLE depthHandle = nullptr;
        auto fail = [&]() {
            if (depthHandle) CloseHandle(depthHandle);
            return false;
        };

        D3D11_TEXTURE2D_DESC transportDesc{};
        transportDesc.Width = width_;
        transportDesc.Height = height_;
        transportDesc.MipLevels = 1;
        transportDesc.ArraySize = 1;
        transportDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        transportDesc.SampleDesc.Count = 1;
        if (!CreateSharedTextureLocked(
                transportDesc, true, sharedDepth,
                depthMutex, depthHandle))
            return fail();

        ComPtr<ID3D11UnorderedAccessView> targetUav;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        if (FAILED(device_->CreateUnorderedAccessView(
                sharedDepth.Get(), &uavDesc, &targetUav)))
            return fail();

        const char* shaderSource = MatchedResidualShaderSource();
        const UINT compileFlags =
            D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_OPTIMIZATION_LEVEL3;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> err;
        ComPtr<ID3D11ComputeShader> captureShader;
        if (FAILED(D3DCompile(
                shaderSource, std::strlen(shaderSource),
                "MatchedResidual.hlsl", nullptr, nullptr,
                "CSCaptureDepth", "cs_5_0", compileFlags, 0,
                &blob, &err)) ||
            FAILED(device_->CreateComputeShader(
                blob->GetBufferPointer(), blob->GetBufferSize(),
                nullptr, &captureShader))) {
            OutputDebugStringA(
                "NRFusion Capture32: D3D11 CSCaptureDepth "
                "compilation/creation failed.\n");
            return fail();
        }

        ComPtr<ID3D11Buffer> captureConstants;
        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = 32;
        constantDesc.Usage = D3D11_USAGE_DEFAULT;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(
                &constantDesc, nullptr, &captureConstants)))
            return fail();

        depthSourceView_ = std::move(sourceView);
        sharedDepth_ = std::move(sharedDepth);
        depthMutex_ = std::move(depthMutex);
        depthTargetUav_ = std::move(targetUav);
        depthCaptureShader_ = std::move(captureShader);
        depthCaptureConstants_ = std::move(captureConstants);
        depthHandle_ = depthHandle;
        depthSourceWidth_ = depthDesc.Width;
        depthSourceHeight_ = depthDesc.Height;
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

