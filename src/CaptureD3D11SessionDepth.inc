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

