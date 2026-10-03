#pragma once

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
#include <DirectXPackedVector.h>

#include "nrfusion/D3D12NrCodec.hpp"
#include "D3D12CodecExactComparison.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nrfusion::testing {

inline void CheckCodecResidualOutlierScenario(ID3D12Device* device, ID3D12CommandQueue* queue) {
    D3D12NrCodec codec;
    assert(codec.Init(device));
    assert(codec.CanApplyResidualInPlace());

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    assert(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))));

    auto source = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, false);
    auto model = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, false);
    auto motion = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, false);
    auto history0 = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, true);
    auto history1 = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, true);
    auto postRrBase = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, false);
    auto stockComposed = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, true);
    auto inPlaceComposed = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, true);

    D3D12_RESOURCE_DESC desc = stockComposed->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
    const UINT64 sliceBytes = (totalBytes + 255) & ~255ULL;

    auto upload = CodecExactBuffer(device, sliceBytes * 5, D3D12_HEAP_TYPE_UPLOAD);
    auto readback = CodecExactBuffer(device, sliceBytes * 2, D3D12_HEAP_TYPE_READBACK);

    uint8_t* mapped = nullptr;
    const D3D12_RANGE empty{0, 0};
    assert(SUCCEEDED(upload->Map(0, &empty, reinterpret_cast<void**>(&mapped))));

    auto toHalf = [](float v) { return DirectX::PackedVector::XMConvertFloatToHalf(v); };

    for (unsigned row = 0; row < 8; ++row) {
        auto* srcRow = reinterpret_cast<uint16_t*>(mapped + row * footprint.Footprint.RowPitch);
        auto* mdlRow = reinterpret_cast<uint16_t*>(mapped + sliceBytes + row * footprint.Footprint.RowPitch);
        auto* motRow = reinterpret_cast<uint16_t*>(mapped + sliceBytes * 2 + row * footprint.Footprint.RowPitch);
        auto* histRow = reinterpret_cast<uint16_t*>(mapped + sliceBytes * 3 + row * footprint.Footprint.RowPitch);
        auto* baseRow = reinterpret_cast<uint16_t*>(mapped + sliceBytes * 4 + row * footprint.Footprint.RowPitch);

        float s[4]{0.015f, 0.015f, 0.015f, 1.0f};
        float m[4]{0.015f, 0.015f, 0.015f, 1.0f};
        float b[4]{0.015f, 0.015f, 0.015f, 1.0f};

        if (row == 1) { // Severe specular magenta firefly in model
            m[0] = 0.25f; m[1] = 0.002f; m[2] = 0.25f; b[0] = 0.018f; b[1] = 0.018f; b[2] = 0.018f;
        } else if (row == 2) { // Green ray-tracing spike in source (negative green delta)
            s[0] = 0.01f; s[1] = 0.25f; s[2] = 0.01f; m[0] = 0.01f; m[1] = 0.01f; m[2] = 0.01f;
            b[0] = 0.015f; b[1] = 0.015f; b[2] = 0.015f;
        } else if (row == 3) { // Normal subtle detail enhancement (+10%)
            s[0] = 0.15f; s[1] = 0.15f; s[2] = 0.15f; m[0] = 0.165f; m[1] = 0.165f; m[2] = 0.165f;
            b[0] = 0.15f; b[1] = 0.15f; b[2] = 0.15f;
        } else if (row == 4) { // Normal HDR highlight
            s[0] = 2.0f; s[1] = 2.0f; s[2] = 2.0f; m[0] = 2.2f; m[1] = 2.2f; m[2] = 2.2f;
            b[0] = 2.0f; b[1] = 2.0f; b[2] = 2.0f;
        }

        for (unsigned col = 0; col < 8; ++col) {
            for (unsigned ch = 0; ch < 4; ++ch) {
                srcRow[col * 4 + ch] = toHalf(s[ch]);
                mdlRow[col * 4 + ch] = toHalf(m[ch]);
                motRow[col * 4 + ch] = toHalf(0.0f);
                histRow[col * 4 + ch] = toHalf(0.0f);
                baseRow[col * 4 + ch] = toHalf(b[ch]);
            }
        }
    }
    upload->Unmap(0, nullptr);

    auto uploadTexture = [&](ID3D12Resource* dst, UINT64 offset) {
        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = upload.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint = footprint;
        srcLoc.PlacedFootprint.Offset = offset;
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dst;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
    };

    uploadTexture(source.Get(), 0);
    uploadTexture(model.Get(), sliceBytes);
    uploadTexture(motion.Get(), sliceBytes * 2);
    uploadTexture(history0.Get(), sliceBytes * 3);
    uploadTexture(postRrBase.Get(), sliceBytes * 4);

    CodecExactTransition(list.Get(), source.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CodecExactTransition(list.Get(), model.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CodecExactTransition(list.Get(), motion.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CodecExactTransition(list.Get(), history0.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CodecExactTransition(list.Get(), history1.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CodecExactTransition(list.Get(), postRrBase.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CodecExactTransition(list.Get(), stockComposed.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CodecExactTransition(list.Get(), inPlaceComposed.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    assert(SUCCEEDED(list->Close()));
    ID3D12CommandList* lists[]{list.Get()};
    queue->ExecuteCommandLists(1, lists);

    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT64 completion = 0;
    assert(SUCCEEDED(queue->Signal(fence.Get(), ++completion)));
    assert(SUCCEEDED(fence->SetEventOnCompletion(completion, completed)));
    assert(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0);
    assert(SUCCEEDED(allocator->Reset()));
    assert(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));

    for (float blend : {0.01f, 0.08f, 0.25f, 1.0f}) {
        D3D12NrCodecConstants accum{};
        accum.mode = 0;
        accum.width = 8;
        accum.height = 8;
        accum.residualBlend = blend;
        accum.residualHistoryValid = 0;
        accum.guideWidth = 8;
        accum.guideHeight = 8;
        accum.maxRatio = 4.0f;
        accum.colourStrength = 1.0f;

        D3D12NrCodecResources accumRes{};
        accumRes.source = source.Get();
        accumRes.model = model.Get();
        accumRes.original = history0.Get();
        accumRes.motion = motion.Get();
        accumRes.target = history1.Get();
        assert(codec.DispatchResidual(list.Get(), accum, accumRes));

        CodecExactTransition(list.Get(), history1.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        CodecExactTransition(list.Get(), postRrBase.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        CodecExactTransition(list.Get(), stockComposed.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        CodecExactTransition(list.Get(), inPlaceComposed.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);

        list->CopyResource(stockComposed.Get(), postRrBase.Get());
        list->CopyResource(inPlaceComposed.Get(), postRrBase.Get());

        CodecExactTransition(list.Get(), postRrBase.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        CodecExactTransition(list.Get(), stockComposed.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CodecExactTransition(list.Get(), inPlaceComposed.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        D3D12NrCodecConstants apply{};
        apply.mode = 1;
        apply.width = 8;
        apply.height = 8;
        apply.transferStrength = 1.0f;
        apply.maxRatio = 4.0f;
        apply.colourStrength = 1.0f;

        D3D12NrCodecResources stockRes{};
        stockRes.source = postRrBase.Get();
        stockRes.model = history1.Get();
        stockRes.target = stockComposed.Get();
        assert(codec.DispatchResidual(list.Get(), apply, stockRes));

        D3D12NrCodecResources inPlaceRes{};
        inPlaceRes.source = inPlaceComposed.Get();
        inPlaceRes.model = history1.Get();
        inPlaceRes.target = inPlaceComposed.Get();
        assert(codec.DispatchResidual(list.Get(), apply, inPlaceRes));

        CodecExactTransition(list.Get(), stockComposed.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        CodecExactTransition(list.Get(), inPlaceComposed.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION rbLoc{};
        rbLoc.pResource = readback.Get();
        rbLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        rbLoc.PlacedFootprint = footprint;

        srcLoc.pResource = stockComposed.Get();
        list->CopyTextureRegion(&rbLoc, 0, 0, 0, &srcLoc, nullptr);
        rbLoc.PlacedFootprint.Offset = sliceBytes;
        srcLoc.pResource = inPlaceComposed.Get();
        list->CopyTextureRegion(&rbLoc, 0, 0, 0, &srcLoc, nullptr);

        CodecExactTransition(list.Get(), stockComposed.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CodecExactTransition(list.Get(), inPlaceComposed.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CodecExactTransition(list.Get(), history1.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        assert(SUCCEEDED(list->Close()));
        ID3D12CommandList* submitted[]{list.Get()};
        queue->ExecuteCommandLists(1, submitted);
        assert(SUCCEEDED(queue->Signal(fence.Get(), ++completion)));
        assert(SUCCEEDED(fence->SetEventOnCompletion(completion, completed)));
        assert(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0);

        D3D12_RANGE read{0, static_cast<SIZE_T>(sliceBytes * 2)};
        assert(SUCCEEDED(readback->Map(0, &read, reinterpret_cast<void**>(&mapped))));

        for (unsigned r = 0; r < 8; ++r) {
            auto* stockPtr = reinterpret_cast<uint16_t*>(mapped + r * footprint.Footprint.RowPitch);
            auto* inPlacePtr = reinterpret_cast<uint16_t*>(mapped + sliceBytes + r * footprint.Footprint.RowPitch);
            // Parity between stock compose and in-place
            assert(std::memcmp(stockPtr, inPlacePtr, 8 * 4 * sizeof(uint16_t)) == 0);

            for (unsigned c = 0; c < 8; ++c) {
                float rf = DirectX::PackedVector::XMConvertHalfToFloat(stockPtr[c * 4 + 0]);
                float gf = DirectX::PackedVector::XMConvertHalfToFloat(stockPtr[c * 4 + 1]);
                float bf = DirectX::PackedVector::XMConvertHalfToFloat(stockPtr[c * 4 + 2]);
                assert(std::isfinite(rf) && std::isfinite(gf) && std::isfinite(bf));
                assert(rf >= 0.0f && gf >= 0.0f && bf >= 0.0f);

                if (r == 1) { // Row 1: Specular firefly -> must NOT produce magenta spike
                    float minRb = std::min(rf, bf);
                    assert(gf > 0.001f);
                    assert(minRb <= 2.5f * gf);
                } else if (r == 2) { // Row 2: Green spike in source -> green must NOT collapse to zero
                    assert(gf > 0.001f);
                    float minRb = std::min(rf, bf);
                    assert(minRb <= 5.0f * gf);
                } else if (r == 3) { // Row 3: Normal detail -> preserved
                    assert(std::abs(rf - 0.165f) < 0.02f);
                }
            }
        }
        readback->Unmap(0, &empty);
        assert(SUCCEEDED(allocator->Reset()));
        assert(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));
    }
    CloseHandle(completed);
}

} // namespace nrfusion::testing
