    // 7. ExtractResidual Numerical Correctness (Fase 3: matched residual = neuralOut - colour)
    {
        // Block 6 closed and executed cmdList already; reopen it for this block's own recording.
        assert(SUCCEEDED(alloc->Reset()));
        assert(SUCCEEDED(cmdList->Reset(alloc.Get(), nullptr)));

        auto fillConstant = [&](ID3D12Resource* target, UINT width, UINT height, float value) {
            D3D12_RESOURCE_DESC desc = target->GetDesc();
            UINT64 uploadSize = 0;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bufferDesc{};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = uploadSize;
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> upload;
            assert(SUCCEEDED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                             IID_PPV_ARGS(&upload))));

            std::vector<uint16_t> halfRow(width * 4);
            const uint16_t halfValue = DirectX::PackedVector::XMConvertFloatToHalf(value);
            for (auto& c : halfRow) c = halfValue;

            uint8_t* mapped = nullptr;
            assert(SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))));
            for (UINT y = 0; y < height; ++y) {
                std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                           halfRow.data(), halfRow.size() * sizeof(uint16_t));
            }
            upload->Unmap(0, nullptr);

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = target;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = footprint;

            D3D12_RESOURCE_BARRIER toDst{};
            toDst.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toDst.Transition.pResource = target;
            toDst.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            toDst.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            toDst.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            cmdList->ResourceBarrier(1, &toDst);
            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            D3D12_RESOURCE_BARRIER toCommon = toDst;
            std::swap(toCommon.Transition.StateBefore, toCommon.Transition.StateAfter);
            cmdList->ResourceBarrier(1, &toCommon);
            return upload; // must outlive the command list's execution
        };

        ID3D12Resource* lowColor = provider.GetSlotLowColor(0);
        ID3D12Resource* lowNeuralOut = provider.GetSlotLowNeuralOut(0);
        ID3D12Resource* lowResidual = provider.GetSlotLowResidual(0);
        assert(lowColor && lowNeuralOut && lowResidual);

        SyntheticWorkHandle h4{};
        h4.workId = 1004; // matches the still-active slot 0 ticket from block 4 above
        h4.valid = true;
        h4.workResolution = { 1440, 810 };

        auto keepAliveColor = fillConstant(lowColor, 1440, 810, 0.25f);
        auto keepAliveNeural = fillConstant(lowNeuralOut, 1440, 810, 0.40f);

        bool extracted = provider.ExtractResidual(h4, cmdList.Get());
        assert(extracted);

        D3D12_RESOURCE_BARRIER toSrc{};
        toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrc.Transition.pResource = lowResidual;
        toSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cmdList->ResourceBarrier(1, &toSrc);

        D3D12_RESOURCE_DESC residualDesc = lowResidual->GetDesc();
        UINT64 readbackSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT readbackFootprint{};
        device->GetCopyableFootprints(&residualDesc, 0, 1, 0, &readbackFootprint, nullptr, nullptr, &readbackSize);

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC readbackBufDesc{};
        readbackBufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readbackBufDesc.Width = readbackSize;
        readbackBufDesc.Height = 1;
        readbackBufDesc.DepthOrArraySize = 1;
        readbackBufDesc.MipLevels = 1;
        readbackBufDesc.Format = DXGI_FORMAT_UNKNOWN;
        readbackBufDesc.SampleDesc.Count = 1;
        readbackBufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ComPtr<ID3D12Resource> readback;
        assert(SUCCEEDED(device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackBufDesc,
                                                         D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                         IID_PPV_ARGS(&readback))));

        D3D12_TEXTURE_COPY_LOCATION rbDst{};
        rbDst.pResource = readback.Get();
        rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        rbDst.PlacedFootprint = readbackFootprint;
        D3D12_TEXTURE_COPY_LOCATION rbSrc{};
        rbSrc.pResource = lowResidual;
        rbSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmdList->CopyTextureRegion(&rbDst, 0, 0, 0, &rbSrc, nullptr);

        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        queue->ExecuteCommandLists(1, lists);

        ComPtr<ID3D12Fence> waitFence2;
        assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&waitFence2))));
        queue->Signal(waitFence2.Get(), 1);
        HANDLE evt2 = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        waitFence2->SetEventOnCompletion(1, evt2);
        WaitForSingleObject(evt2, 2000);
        CloseHandle(evt2);

        uint8_t* mappedReadback = nullptr;
        D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(readbackSize) };
        assert(SUCCEEDED(readback->Map(0, &readRange, reinterpret_cast<void**>(&mappedReadback))));
        const uint16_t* firstTexel = reinterpret_cast<const uint16_t*>(mappedReadback + readbackFootprint.Offset);
        const float residualR = DirectX::PackedVector::XMConvertHalfToFloat(firstTexel[0]);
        readback->Unmap(0, nullptr);

        // 0.40 (neuralOut) - 0.25 (colour) = 0.15, the matched residual CSExtractResidual computes.
        assert(std::abs(residualR - 0.15f) < 0.01f);
        std::cout << "  [PASS] ExtractResidual computed the matched residual correctly (" << residualR
                  << " ~= 0.15)." << std::endl;
    }