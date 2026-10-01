#pragma once
#include "nrfusion/D3D12NrCodec.hpp"
#include <wrl/client.h>
#include <array>
#include <cassert>
#include <cstring>

namespace nrfusion::testing {
inline Microsoft::WRL::ComPtr<ID3D12Resource> CodecExactTexture(
    ID3D12Device* device, D3D12_RESOURCE_STATES state, bool uav) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = desc.Height = 8;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, state, nullptr, IID_PPV_ARGS(&resource))));
    return resource;
}

inline Microsoft::WRL::ComPtr<ID3D12Resource> CodecExactBuffer(
    ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const auto state = type == D3D12_HEAP_TYPE_UPLOAD
        ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, state, nullptr, IID_PPV_ARGS(&resource))));
    return resource;
}

inline void CodecExactTransition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &barrier);
}

inline void CheckCodecEncodeExact(ID3D12Device* device, ID3D12CommandQueue* queue) {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    assert(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                              nullptr, IID_PPV_ARGS(&list))));
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    D3D12NrCodec codec;
    assert(codec.Init(device) && codec.CanSkipKeep());
    auto source = CodecExactTexture(device, D3D12_RESOURCE_STATE_COPY_DEST, false);
    auto stock = CodecExactTexture(device, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    auto candidate = CodecExactTexture(device, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    auto keep = CodecExactTexture(device, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    const auto desc = source->GetDesc();
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
    auto upload = CodecExactBuffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD);
    const UINT64 sliceBytes = (bytes + 511) & ~UINT64{511};
    auto readback = CodecExactBuffer(device, sliceBytes * 2, D3D12_HEAP_TYPE_READBACK);
    unsigned char* mapped = nullptr;
    D3D12_RANGE empty{0, 0};
    assert(SUCCEEDED(upload->Map(0, &empty, reinterpret_cast<void**>(&mapped))));
    constexpr std::array<std::uint16_t, 16> patterns{
        0x0000, 0x8000, 0x0001, 0x03ff, 0x0400, 0x1000, 0x3800, 0x3c00,
        0xbc00, 0x4400, 0x7bff, 0xfbff, 0x7c00, 0xfc00, 0x7e00, 0xfe00};
    for (unsigned row = 0; row < 8; ++row)
        for (unsigned component = 0; component < 32; ++component) {
            const auto bits = patterns[(row * 7 + component) % patterns.size()];
            std::memcpy(mapped + row * footprint.Footprint.RowPitch + component * 2, &bits, 2);
        }
    upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION uploadLocation{};
    uploadLocation.pResource = upload.Get();
    uploadLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    uploadLocation.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
    sourceLocation.pResource = source.Get();
    sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&sourceLocation, 0, 0, 0, &uploadLocation, nullptr);
    CodecExactTransition(list.Get(), source.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(completed);
    UINT64 completion = 0;
    for (unsigned caseIndex = 0; caseIndex < 60; ++caseIndex) {
        D3D12NrCodecConstants constants{};
        constants.width = constants.height = 8;
        constants.passthrough = caseIndex % 2;
        constants.reversibleMode = (caseIndex / 2) % 5;
        constexpr float whitePoints[]{0.01f, 1.0f, 8.0f};
        constants.whitePoint = whitePoints[(caseIndex / 10) % 3];
        constants.useGameExposure = caseIndex / 30;
        D3D12NrCodecResources resources{};
        resources.source = source.Get();
        resources.previousEdit = source.Get();
        resources.target = stock.Get();
        resources.keep = keep.Get();
        assert(codec.Dispatch(list.Get(), constants, resources));
        resources.target = candidate.Get();
        resources.keep = nullptr;
        assert(codec.Dispatch(list.Get(), constants, resources));
        ID3D12Resource* outputs[]{stock.Get(), candidate.Get()};
        for (unsigned index = 0; index < 2; ++index) {
            CodecExactTransition(list.Get(), outputs[index], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION outputLocation{};
            outputLocation.pResource = outputs[index];
            outputLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION destination = uploadLocation;
            destination.pResource = readback.Get();
            destination.PlacedFootprint.Offset = index * sliceBytes;
            list->CopyTextureRegion(&destination, 0, 0, 0, &outputLocation, nullptr);
            CodecExactTransition(list.Get(), outputs[index], D3D12_RESOURCE_STATE_COPY_SOURCE,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        assert(SUCCEEDED(list->Close()));
        ID3D12CommandList* submitted[]{list.Get()};
        queue->ExecuteCommandLists(1, submitted);
        assert(SUCCEEDED(queue->Signal(fence.Get(), ++completion)));
        assert(SUCCEEDED(fence->SetEventOnCompletion(completion, completed)));
        assert(WaitForSingleObject(completed, 5000) == WAIT_OBJECT_0);
        assert(device->GetDeviceRemovedReason() == S_OK);
        D3D12_RANGE read{0, static_cast<SIZE_T>(sliceBytes * 2)};
        assert(SUCCEEDED(readback->Map(0, &read, reinterpret_cast<void**>(&mapped))));
        for (unsigned row = 0; row < 8; ++row)
            assert(std::memcmp(mapped + row * footprint.Footprint.RowPitch,
                mapped + sliceBytes + row * footprint.Footprint.RowPitch, 64) == 0);
        readback->Unmap(0, &empty);
        assert(SUCCEEDED(allocator->Reset()));
        assert(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));
    }
    CloseHandle(completed);
}
} // namespace nrfusion::testing
