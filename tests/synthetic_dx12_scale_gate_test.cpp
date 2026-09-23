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

namespace {

struct TestGpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE eventHandle = nullptr;
    uint64_t fenceValue = 0;

    ~TestGpu() {
        if (eventHandle) CloseHandle(eventHandle);
    }

    void ExecuteAndWait() {
        assert(SUCCEEDED(list->Close()));
        ID3D12CommandList* commandLists[] = {list.Get()};
        queue->ExecuteCommandLists(1, commandLists);
        const uint64_t value = ++fenceValue;
        assert(SUCCEEDED(queue->Signal(fence.Get(), value)));
        if (fence->GetCompletedValue() < value) {
            assert(SUCCEEDED(fence->SetEventOnCompletion(value, eventHandle)));
            assert(WaitForSingleObject(eventHandle, 2000) == WAIT_OBJECT_0);
        }
    }

    void Reset() {
        assert(SUCCEEDED(allocator->Reset()));
        assert(SUCCEEDED(list->Reset(allocator.Get(), nullptr)));
    }
};

ComPtr<ID3D12Resource> CreateTexture(ID3D12Device* device, UINT width, UINT height) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = 1;
    description.MipLevels = 1;
    description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    description.SampleDesc.Count = 1;
    description.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> texture;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                     D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                     IID_PPV_ARGS(&texture))));
    return texture;
}

ComPtr<ID3D12Resource> CreateConstantTextureUpload(ID3D12Device* device, UINT width, UINT height,
                                                    uint16_t halfValue) {
    D3D12_RESOURCE_DESC textureDescription{};
    textureDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDescription.Width = width;
    textureDescription.Height = height;
    textureDescription.DepthOrArraySize = 1;
    textureDescription.MipLevels = 1;
    textureDescription.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    textureDescription.SampleDesc.Count = 1;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&textureDescription, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = uploadSize;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.Format = DXGI_FORMAT_UNKNOWN;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&upload))));
    std::vector<uint16_t> row(static_cast<size_t>(width) * 4, halfValue);
    uint8_t* mapped = nullptr;
    assert(SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped))));
    for (UINT y = 0; y < height; ++y) {
        std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                    row.data(), row.size() * sizeof(uint16_t));
    }
    upload->Unmap(0, nullptr);
    return upload;
}

float ReadFirstHalf(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12Resource* texture,
                    TestGpu& gpu) {
    const D3D12_RESOURCE_DESC description = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readbackSize = 0;
    device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &readbackSize);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = readbackSize;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.Format = DXGI_FORMAT_UNKNOWN;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                     IID_PPV_ARGS(&readback))));

    D3D12_RESOURCE_BARRIER toSource{};
    toSource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSource.Transition.pResource = texture;
    toSource.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toSource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toSource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &toSource);

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = texture;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;
    list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    D3D12_RESOURCE_BARRIER toCommon = toSource;
    std::swap(toCommon.Transition.StateBefore, toCommon.Transition.StateAfter);
    list->ResourceBarrier(1, &toCommon);
    gpu.ExecuteAndWait();

    uint8_t* mapped = nullptr;
    assert(SUCCEEDED(readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped))));
    const uint16_t half = *reinterpret_cast<const uint16_t*>(mapped + footprint.Offset);
    readback->Unmap(0, nullptr);
    return nrfusion::testing::HalfToFloat(half);
}

bool InitializeGpu(TestGpu& gpu) {
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; SUCCEEDED(factory->EnumAdapterByGpuPreference(
             index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))); ++index) {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if (!(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
        adapter.Reset();
    }
    if (!adapter || FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                              IID_PPV_ARGS(&gpu.device)))) return false;

    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(gpu.device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&gpu.queue))) ||
        FAILED(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator))) ||
        FAILED(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr,
                                              IID_PPV_ARGS(&gpu.list))) ||
        FAILED(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)))) return false;
    gpu.list->Close();
    gpu.eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return gpu.eventHandle != nullptr;
}

bool RunScaleGate(TestGpu& gpu, SyntheticDx12Provider& provider, float scale,
                  ID3D12Resource* input, UINT expectedWidth, UINT expectedHeight) {
    gpu.Reset();
    SyntheticFrameInputs inputs{};
    inputs.ticket.id = scale < 1.0f ? 2002 : 2001;
    inputs.ticket.session = 1;
    inputs.frameId = inputs.ticket.id;
    inputs.renderResolution = {64, 64};
    inputs.targetResolution = {64, 64};
    inputs.workingScale = scale;
    inputs.color.opaqueId = reinterpret_cast<uint64_t>(input);
    inputs.color.resolution = {64, 64};
    inputs.color.format = ResourceFormat::Rgba16Float;

    const SyntheticWorkHandle handle = provider.Submit(inputs, gpu.list.Get());
    if (!handle.valid || handle.workResolution.width != expectedWidth ||
        handle.workResolution.height != expectedHeight) return false;
    const uint32_t slot = provider.SlotForWork(handle.workId);
    ID3D12Resource* lowColor = provider.GetSlotLowColor(slot);
    if (!lowColor) return false;
    const float firstChannel = ReadFirstHalf(gpu.device.Get(), gpu.list.Get(), lowColor, gpu);
    return std::abs(firstChannel - 0.25f) < 0.01f;
}

} // namespace

int main() {
    std::cout << "[Synthetic Dx12 Scale Gate] WorkingScale 1.0 and reduced GPU copy\n";
    TestGpu gpu;
    if (!InitializeGpu(gpu)) return 1;

    SyntheticDx12Provider provider;
    ProviderContext context{};
    context.api = GraphicsApi::D3D12;
    context.device = gpu.device.Get();
    context.commandQueue = gpu.queue.Get();
    if (!provider.Initialize(context)) return 1;

    ComPtr<ID3D12Resource> input = CreateTexture(gpu.device.Get(), 64, 64);
    const uint16_t halfValue = nrfusion::testing::FloatToHalf(0.25f);
    ComPtr<ID3D12Resource> upload = CreateConstantTextureUpload(gpu.device.Get(), 64, 64, halfValue);
    gpu.Reset();
    D3D12_RESOURCE_DESC inputDescription = input->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 uploadSize = 0;
    gpu.device->GetCopyableFootprints(&inputDescription, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);
    D3D12_RESOURCE_BARRIER toCopy{};
    toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy.Transition.pResource = input.Get();
    toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    gpu.list->ResourceBarrier(1, &toCopy);
    D3D12_TEXTURE_COPY_LOCATION uploadSource{};
    uploadSource.pResource = upload.Get();
    uploadSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    uploadSource.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION inputDestination{};
    inputDestination.pResource = input.Get();
    inputDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gpu.list->CopyTextureRegion(&inputDestination, 0, 0, 0, &uploadSource, nullptr);
    std::swap(toCopy.Transition.StateBefore, toCopy.Transition.StateAfter);
    gpu.list->ResourceBarrier(1, &toCopy);
    gpu.ExecuteAndWait();

    if (!RunScaleGate(gpu, provider, 1.0f, input.Get(), 64, 64) ||
        !RunScaleGate(gpu, provider, 0.5f, input.Get(), 32, 32)) return 1;
    std::cout << "[Synthetic Dx12 Scale Gate] PASS: scale 1.0 initializes lowColor and 0.5 downsamples GPU-only.\n";
    return 0;
}
