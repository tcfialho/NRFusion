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

void ExpectSize(ID3D12Resource* resource, UINT64 width, UINT height) {
    assert(resource != nullptr);
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    assert(desc.Width == width);
    assert(desc.Height == height);
}

} // namespace

int main() {
    using namespace nrfusion;

    D3D12NrScratchResources scratch;
    NrDeferredRetirementQueue retirement;
    const auto invalidKind = static_cast<D3D12NrScratchKind>(0xff);
    const D3D12NrScratchDesc native{
        DXGI_FORMAT_R16G16B16A16_FLOAT, 1920, 1080, 1280, 720};
    const D3D12NrScratchDesc resized{
        DXGI_FORMAT_R16G16B16A16_FLOAT, 1600, 900, 1200, 675};

    assert(!scratch.Ensure(nullptr, native, retirement));
    assert(scratch.Get(invalidKind) == nullptr);
    assert(scratch.State(invalidKind) == D3D12_RESOURCE_STATE_COMMON);
    assert(!scratch.Retire(invalidKind, retirement));

    TestGpu gpu = CreateWarpGpu();
    assert(scratch.Ensure(gpu.device.Get(), native, retirement));
    assert(scratch.Complete());
    ExpectSize(scratch.Get(D3D12NrScratchKind::Output), 1280, 720);
    ExpectSize(scratch.Get(D3D12NrScratchKind::ColorCopy), 1920, 1080);

    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::PassScratch,
        native.format, native.workWidth, native.workHeight, retirement));
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::ColorSmall,
        native.format, native.workWidth, native.workHeight, retirement));
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::OutputNative,
        native.format, native.frameWidth, native.frameHeight, retirement));
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::ActiveColor,
        native.format, native.frameWidth, native.frameHeight, retirement));
    assert(!scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::Output,
        native.format, native.workWidth, native.workHeight, retirement));
    assert(retirement.Size() == 0);

    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::PassScratch,
        native.format, native.workWidth, native.workHeight, retirement));
    assert(retirement.Size() == 0);
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::PassScratch,
        native.format, 640, 360, retirement));
    assert(retirement.Size() == 1);
    ExpectSize(scratch.Get(D3D12NrScratchKind::PassScratch), 640, 360);

    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::PassScratch,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(!scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::PassScratch,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::PassScratch,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    assert(scratch.Retire(D3D12NrScratchKind::ActiveColor, retirement));
    assert(scratch.Get(D3D12NrScratchKind::ActiveColor) == nullptr);
    assert(retirement.Size() == 2);

    assert(scratch.Ensure(gpu.device.Get(), resized, retirement));
    assert(scratch.Matches(resized));
    assert(scratch.Get(D3D12NrScratchKind::PassScratch) == nullptr);
    assert(scratch.Get(D3D12NrScratchKind::ColorSmall) == nullptr);
    assert(scratch.Get(D3D12NrScratchKind::OutputNative) == nullptr);
    assert(retirement.Size() == 8);

    assert(scratch.Retire(retirement));
    assert(!scratch.Complete());
    assert(retirement.Size() == 11);

    assert(SUCCEEDED(gpu.list->Close()));
    unsigned released = 0;
    retirement.DrainAfterIdle(&released, &ReleaseRetired);
    assert(released == 11);
    assert(retirement.Size() == 0);

    scratch.ReleaseAfterIdle();
    return 0;
}
