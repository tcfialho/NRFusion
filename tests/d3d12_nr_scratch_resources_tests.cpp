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

#include "nrfusion/D3D12NrScratchResources.hpp"

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

} // namespace

int main() {
    using namespace nrfusion;

    D3D12NrScratchResources scratch;
    NrDeferredRetirementQueue retirement;

    const D3D12NrScratchDesc valid{
        DXGI_FORMAT_R16G16B16A16_FLOAT, 1920, 1080, 1280, 720};
    const D3D12NrScratchDesc resized{
        DXGI_FORMAT_R16G16B16A16_FLOAT, 1600, 900, 1200, 675};
    const D3D12NrScratchDesc invalid{
        DXGI_FORMAT_UNKNOWN, 1920, 1080, 1280, 720};
    const auto invalidKind = static_cast<D3D12NrScratchKind>(0xff);

    assert(!scratch.Complete());
    assert(!scratch.Matches(valid));
    assert(scratch.Get(D3D12NrScratchKind::Output) == nullptr);
    assert(scratch.Get(D3D12NrScratchKind::ColorCopy) == nullptr);
    assert(scratch.Get(D3D12NrScratchKind::HdrCopy) == nullptr);
    assert(scratch.Get(invalidKind) == nullptr);
    assert(scratch.State(D3D12NrScratchKind::Output) ==
           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(scratch.State(invalidKind) == D3D12_RESOURCE_STATE_COMMON);

    assert(!scratch.Ensure(nullptr, valid, retirement));
    assert(!scratch.Ensure(nullptr, invalid, retirement));
    assert(retirement.Size() == 0);
    assert(!scratch.Transition(
        nullptr, invalidKind,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    TestGpu gpu = CreateWarpGpu();
    assert(scratch.Ensure(gpu.device.Get(), valid, retirement));
    assert(scratch.Complete());
    assert(scratch.Matches(valid));
    assert(retirement.Size() == 0);
    assert(scratch.Ensure(gpu.device.Get(), valid, retirement));
    assert(retirement.Size() == 0);

    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::Output,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(scratch.State(D3D12NrScratchKind::Output) ==
           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    assert(!scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::Output,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::Output,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    assert(scratch.Ensure(gpu.device.Get(), resized, retirement));
    assert(scratch.Matches(resized));
    assert(retirement.Size() == 3);
    assert(scratch.Retire(retirement));
    assert(!scratch.Complete());
    assert(retirement.Size() == 6);

    assert(SUCCEEDED(gpu.list->Close()));
    unsigned released = 0;
    retirement.DrainAfterIdle(&released, &ReleaseRetired);
    assert(released == 6);
    assert(retirement.Size() == 0);

    scratch.ReleaseAfterIdle();
    return 0;
}
