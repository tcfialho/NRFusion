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

#include "nrfusion/D3D12NrCodec.hpp"

#include <cassert>

using Microsoft::WRL::ComPtr;

namespace {

ComPtr<ID3D12Resource> Texture(
    ID3D12Device* device, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 8;
    desc.Height = 8;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    assert(SUCCEEDED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(&resource))));
    return resource;
}

} // namespace

int main() {
    using namespace nrfusion;

    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;

    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D12CreateDevice(
        adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    assert(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    assert(SUCCEEDED(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    assert(SUCCEEDED(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&list))));
    assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));

    D3D12NrCodec codec;
    assert(codec.Init(device.Get()));
    assert(codec.Ready());

    auto source = Texture(
        device.Get(), D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto target = Texture(
        device.Get(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12NrCodecConstants constants{};
    constants.mode = static_cast<std::uint32_t>(D3D12NrCodecMode::Encode);
    constants.width = 8;
    constants.height = 8;
    constants.guideWidth = 8;
    constants.guideHeight = 8;
    constants.passthrough = 1;

    D3D12NrCodecResources resources{};
    resources.source = source.Get();
    resources.target = target.Get();

    assert(codec.Dispatch(list.Get(), constants, resources));
    assert(SUCCEEDED(list->Close()));

    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);
    assert(SUCCEEDED(queue->Signal(fence.Get(), 1)));

    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(eventHandle != nullptr);
    assert(SUCCEEDED(fence->SetEventOnCompletion(1, eventHandle)));
    assert(WaitForSingleObject(eventHandle, 5000) == WAIT_OBJECT_0);
    CloseHandle(eventHandle);

    assert(device->GetDeviceRemovedReason() == S_OK);
    codec.Shutdown();
    return 0;
}
