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

#include "nrfusion/D3D12NrGuideClones.hpp"

#include <cassert>

using Microsoft::WRL::ComPtr;

namespace {

void ReleaseRetired(void* context, nrfusion::NrRetiredObject retired) noexcept {
    if (retired.object == nullptr || retired.kind != nrfusion::NrRetiredObjectKind::Resource) return;
    static_cast<ID3D12Resource*>(retired.object)->Release();
    ++*static_cast<unsigned*>(context);
}

struct TestGpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
};

TestGpu CreateWarpGpu() {
    TestGpu gpu;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    assert(SUCCEEDED(D3D12CreateDevice(
        adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device))));
    assert(SUCCEEDED(gpu.device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator))));
    assert(SUCCEEDED(gpu.device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr,
        IID_PPV_ARGS(&gpu.list))));
    return gpu;
}

ComPtr<ID3D12Resource> CreateSource(
    ID3D12Device* device, DXGI_FORMAT format, UINT64 width, UINT height) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;

    ComPtr<ID3D12Resource> resource;
    assert(SUCCEEDED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        nullptr, IID_PPV_ARGS(&resource))));
    return resource;
}

} // namespace

int main() {
    using namespace nrfusion;

    TestGpu gpu = CreateWarpGpu();
    NrDeferredRetirementQueue retirement;
    D3D12NrGuideClones clones;
    const auto invalidKind = static_cast<D3D12NrGuideKind>(0xff);

    auto depth = CreateSource(gpu.device.Get(), DXGI_FORMAT_R32_TYPELESS, 1280, 720);
    auto motion = CreateSource(gpu.device.Get(), DXGI_FORMAT_R16G16_TYPELESS, 1280, 720);

    assert(!clones.Ensure(nullptr, D3D12NrGuideKind::Depth,
                          depth.Get(), DXGI_FORMAT_R32_FLOAT, retirement));
    assert(!clones.Ensure(gpu.device.Get(), invalidKind,
                          depth.Get(), DXGI_FORMAT_R32_FLOAT, retirement));
    assert(clones.Get(invalidKind) == nullptr);

    assert(clones.Ensure(gpu.device.Get(), D3D12NrGuideKind::Depth,
                         depth.Get(), DXGI_FORMAT_R32_FLOAT, retirement));
    assert(clones.Ensure(gpu.device.Get(), D3D12NrGuideKind::Motion,
                         motion.Get(), DXGI_FORMAT_R16G16_FLOAT, retirement));
    assert(retirement.Size() == 0);
    assert(clones.State(D3D12NrGuideKind::Depth) == D3D12_RESOURCE_STATE_COPY_DEST);

    const D3D12_RESOURCE_DESC depthClone = clones.Get(D3D12NrGuideKind::Depth)->GetDesc();
    assert(depthClone.Width == 1280 && depthClone.Height == 720);
    assert(depthClone.Format == DXGI_FORMAT_R32_FLOAT);
    assert(depthClone.Flags == D3D12_RESOURCE_FLAG_NONE);

    assert(clones.Transition(
        gpu.list.Get(), D3D12NrGuideKind::Depth,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(!clones.Transition(
        gpu.list.Get(), D3D12NrGuideKind::Depth,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(clones.Transition(
        gpu.list.Get(), D3D12NrGuideKind::Depth,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_COPY_DEST));

    assert(clones.Ensure(gpu.device.Get(), D3D12NrGuideKind::Depth,
                         depth.Get(), DXGI_FORMAT_R32_FLOAT, retirement));
    assert(retirement.Size() == 0);

    depth = CreateSource(gpu.device.Get(), DXGI_FORMAT_R32_TYPELESS, 960, 540);
    assert(clones.Ensure(gpu.device.Get(), D3D12NrGuideKind::Depth,
                         depth.Get(), DXGI_FORMAT_R32_FLOAT, retirement));
    assert(retirement.Size() == 1);
    assert(clones.Get(D3D12NrGuideKind::Depth)->GetDesc().Width == 960);

    assert(clones.Retire(retirement));
    assert(retirement.Size() == 3);
    assert(clones.Get(D3D12NrGuideKind::Depth) == nullptr);
    assert(clones.Get(D3D12NrGuideKind::Motion) == nullptr);

    assert(SUCCEEDED(gpu.list->Close()));
    unsigned released = 0;
    retirement.DrainAfterIdle(&released, &ReleaseRetired);
    assert(released == 3);

    clones.ReleaseAfterIdle();
    return 0;
}
