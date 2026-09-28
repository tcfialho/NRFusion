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

