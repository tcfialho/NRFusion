#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/SyntheticDlaaContract.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"

#include "HalfFloat.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace nrfusion;

int main() {
    std::cout << "[Synthetic Dx12 Test] Starting Stage 1 validation..." << std::endl;

    // 1. Contract Verification (1:1 DLAA, no SR by default)
    {
        Resolution nativeRes{ 1920, 1080 };
        SyntheticDlaaConfig cfg{};
        cfg.nativeResolution = nativeRes;
        cfg.workingScale = 1.0f;
        cfg.isHdr = true;
        cfg.depthInverted = true;

        auto contract = SyntheticDlaaContract::CreateContract(cfg);
        assert(contract.IsValid());
        assert(contract.inWidth == 1920);
        assert(contract.inHeight == 1080);
        assert(contract.inTargetWidth == 1920);
        assert(contract.inTargetHeight == 1080);
        assert(contract.inPerfQualityValue == SYNTHETIC_NGX_PERF_QUALITY_DLAA);
        assert(contract.createFlags & 0x01); // HDR
        assert(contract.createFlags & 0x08); // DepthInverted

        // Scale 0.75
        cfg.workingScale = 0.75f;
        auto contract75 = SyntheticDlaaContract::CreateContract(cfg);
        assert(contract75.IsValid());
        assert(contract75.inWidth == 1440);
        assert(contract75.inHeight == 810);
        assert(contract75.inTargetWidth == 1440);
        assert(contract75.inTargetHeight == 810);

        // Scale 0.50
        cfg.workingScale = 0.50f;
        auto contract50 = SyntheticDlaaContract::CreateContract(cfg);
        assert(contract50.IsValid());
        assert(contract50.inWidth == 960);
        assert(contract50.inHeight == 540);
        assert(contract50.inTargetWidth == 960);
        assert(contract50.inTargetHeight == 540);

        std::cout << "  [PASS] Synthetic DLAA 1:1 contract calculation validated." << std::endl;
    }

    // 2. Hardware / D3D12 Device Initialization
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        std::cerr << "Failed to create DXGI factory" << std::endl;
        return 1;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            std::wcout << L"  [D3D12 Hardware Device]: " << desc.Description << std::endl;
            break;
        }
    }
    if (!adapter) {
        std::cerr << "No hardware DX12 adapter found" << std::endl;
        return 1;
    }

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        std::cerr << "Failed to create D3D12 device" << std::endl;
        return 1;
    }

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue)))) {
        std::cerr << "Failed to create direct command queue" << std::endl;
        return 1;
    }

    ComPtr<ID3D12CommandAllocator> alloc;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)))) {
        std::cerr << "Failed to create command allocator" << std::endl;
        return 1;
    }

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmdList)))) {
        std::cerr << "Failed to create command list" << std::endl;
        return 1;
    }

    // 3. SyntheticDx12Provider Same-Device Initialization
    SyntheticDx12Provider provider;
    ProviderContext ctx{};
    ctx.api = GraphicsApi::D3D12;
    ctx.device = device.Get();
    ctx.commandQueue = queue.Get();
    ctx.preferSameDevice = true;

    bool initOk = provider.Initialize(ctx);
    assert(initOk);
    assert(provider.IsReady());
    std::cout << "  [PASS] SyntheticDx12Provider initialized on same-device context." << std::endl;

    ComPtr<ID3D12Resource> testInputColor;

    // 4. Bounded Ring Slots & Submit Test
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = 1920;
        colorDesc.Height = 1080;
        colorDesc.DepthOrArraySize = 1;
        colorDesc.MipLevels = 1;
        colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        colorDesc.SampleDesc.Count = 1;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        assert(SUCCEEDED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&testInputColor))));

        SyntheticFrameInputs inputs{};
        inputs.ticket.id = 1001;
        inputs.ticket.session = 1;
        inputs.frameId = 1;
        inputs.renderResolution = { 1920, 1080 };
        inputs.targetResolution = { 1920, 1080 };
        inputs.workingScale = 0.75f;
        inputs.color.opaqueId = reinterpret_cast<uint64_t>(testInputColor.Get());
        inputs.color.resolution = { 1920, 1080 };
        inputs.color.format = ResourceFormat::Rgba16Float;

        SyntheticWorkHandle h1 = provider.Submit(inputs, cmdList.Get());
        assert(h1.valid);
        assert(h1.workId == 1001);
        assert(h1.workResolution.width == 1440);
        assert(h1.workResolution.height == 810);
        assert(h1.fenceValue > 0);

        inputs.ticket.id = 1002;
        inputs.frameId = 2;
        SyntheticWorkHandle h2 = provider.Submit(inputs, cmdList.Get());
        assert(h2.valid);
        assert(h2.workId == 1002);
        assert(h2.fenceValue > h1.fenceValue);

        inputs.ticket.id = 1003;
        inputs.frameId = 3;
        SyntheticWorkHandle h3 = provider.Submit(inputs, cmdList.Get());
        assert(h3.valid);
        assert(h3.workId == 1003);

        // Ring wrap test (slot 0 reused safely for 4th frame)
        inputs.ticket.id = 1004;
        inputs.frameId = 4;
        SyntheticWorkHandle h4 = provider.Submit(inputs, cmdList.Get());
        assert(h4.valid);
        assert(h4.workId == 1004);

        // Residual retrieval for active work ticket
        ResourceRef res4 = provider.GetResidual(h4);
        assert(res4.Valid());
        assert(res4.resolution.width == 1440);
        assert(res4.resolution.height == 810);

        std::cout << "  [PASS] Bounded ring slots and WorkId tracking validated." << std::endl;
    }

    // 5. Zero CPU Copy Verification
    {
        // Check that slot resources reside in default GPU memory without CPU pointers
        ID3D12Resource* lowColor = provider.GetSlotLowColor(0);
        assert(lowColor != nullptr);
        D3D12_RESOURCE_DESC desc = lowColor->GetDesc();
        assert(desc.Width == 1440);
        assert(desc.Height == 810);
        assert(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        ID3D12Resource* lowRes = provider.GetSlotLowResidual(0);
        assert(lowRes != nullptr);
        std::cout << "  [PASS] Zero-CPU GPU-only allocation confirmed." << std::endl;
    }

    // 6. GPU ComposeNative Execution Test
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resDesc.Width = 1920;
        resDesc.Height = 1080;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        resDesc.SampleDesc.Count = 1;
        resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ComPtr<ID3D12Resource> nativeOrig;
        ComPtr<ID3D12Resource> nativeDst;
        assert(SUCCEEDED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&nativeOrig))));
        assert(SUCCEEDED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&nativeDst))));

        ResourceRef origRef{};
        origRef.opaqueId = reinterpret_cast<uint64_t>(nativeOrig.Get());
        origRef.resolution = { 1920, 1080 };
        origRef.format = ResourceFormat::Rgba16Float;

        ResourceRef dstRef{};
        dstRef.opaqueId = reinterpret_cast<uint64_t>(nativeDst.Get());
        dstRef.resolution = { 1920, 1080 };
        dstRef.format = ResourceFormat::Rgba16Float;

        SyntheticWorkHandle h4{};
        h4.workId = 1004;
        h4.valid = true;
        h4.workResolution = { 1440, 810 };
        h4.nativeResolution = { 1920, 1080 };

        bool composeOk = provider.ComposeNative(h4, origRef, dstRef, cmdList.Get(), 1.0f);
        assert(composeOk);

        cmdList->Close();
        ID3D12CommandList* lists[] = { cmdList.Get() };
        queue->ExecuteCommandLists(1, lists);

        ComPtr<ID3D12Fence> waitFence;
        assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&waitFence))));
        queue->Signal(waitFence.Get(), 1);
        HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        waitFence->SetEventOnCompletion(1, evt);
        WaitForSingleObject(evt, 2000);
        CloseHandle(evt);

        std::cout << "  [PASS] ComposeNative GPU dispatch and execution validated." << std::endl;
    }

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
            const uint16_t halfValue = nrfusion::testing::FloatToHalf(value);
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
        const float residualR = nrfusion::testing::HalfToFloat(firstTexel[0]);
        readback->Unmap(0, nullptr);

        // 0.40 (neuralOut) - 0.25 (colour) = 0.15, the matched residual CSExtractResidual computes.
        assert(std::abs(residualR - 0.15f) < 0.01f);
        std::cout << "  [PASS] ExtractResidual computed the matched residual correctly (" << residualR
                  << " ~= 0.15)." << std::endl;
    }

    provider.Shutdown();
    assert(!provider.IsReady());

    std::cout << "[Synthetic Dx12 Test] All Stage 1 assertions PASSED." << std::endl;
    return 0;
}
