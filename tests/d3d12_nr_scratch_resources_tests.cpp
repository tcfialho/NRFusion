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

#include "D3D12TestDevice.hpp"

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

TestGpu CreateTestGpu() {
    TestGpu gpu;
    gpu.device = nrfusion::testing::CreateD3D12TestDevice();
    assert(gpu.device);
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

    TestGpu gpu = CreateTestGpu();
    assert(scratch.Ensure(gpu.device.Get(), native, retirement));
    assert(scratch.Complete());
    ExpectSize(scratch.Get(D3D12NrScratchKind::Output), 1280, 720);
    ExpectSize(scratch.Get(D3D12NrScratchKind::ColorCopy), 1920, 1080);
    constexpr std::uint64_t frameBytes = 1920ull * 1080ull * 8ull;
    constexpr std::uint64_t workBytes = 1280ull * 720ull * 8ull;
    const auto coreAccounting = scratch.Accounting();
    assert(coreAccounting.resourceCount == 3);
    assert(coreAccounting.logicalBytesExact);
    assert(coreAccounting.logicalBytes == workBytes + 2ull * frameBytes);

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
        gpu.device.Get(), D3D12NrScratchKind::ResidualEdited,
        native.format, native.frameWidth, native.frameHeight, retirement));
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::ResidualHistory0,
        DXGI_FORMAT_R16G16B16A16_FLOAT, native.frameWidth, native.frameHeight, retirement));
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::ResidualHistory1,
        DXGI_FORMAT_R16G16B16A16_FLOAT, native.frameWidth, native.frameHeight, retirement));
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::ResidualComposed,
        native.format, native.frameWidth, native.frameHeight, retirement));
    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::ResidualEdited,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(scratch.State(D3D12NrScratchKind::ResidualEdited) ==
           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    assert(retirement.Size() == 0);
    const auto fullAccounting = scratch.Accounting();
    assert(fullAccounting.resourceCount == 11);
    assert(fullAccounting.logicalBytesExact);
    assert(fullAccounting.logicalBytes == 3ull * workBytes + 8ull * frameBytes);

    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::PassScratch,
        native.format, native.workWidth, native.workHeight, retirement));
    assert(retirement.Size() == 0);
    assert(scratch.EnsureOptional(
        gpu.device.Get(), D3D12NrScratchKind::PassScratch,
        native.format, 640, 360, retirement));
    assert(retirement.Size() == 1);
    auto retiredAccounting = retirement.ResourceAccounting();
    assert(retiredAccounting.resourceCount == 1);
    assert(retiredAccounting.logicalBytes == workBytes);
    assert(retiredAccounting.logicalBytesExact);
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
    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::PassScratch,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(scratch.Transition(
        gpu.list.Get(), D3D12NrScratchKind::Output,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    assert(scratch.RestoreAllToUav(gpu.list.Get()));
    assert(scratch.State(D3D12NrScratchKind::PassScratch) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(scratch.State(D3D12NrScratchKind::Output) == D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    assert(scratch.Retire(D3D12NrScratchKind::ActiveColor, retirement));
    assert(scratch.Get(D3D12NrScratchKind::ActiveColor) == nullptr);
    assert(retirement.Size() == 2);
    retiredAccounting = retirement.ResourceAccounting();
    assert(retiredAccounting.resourceCount == 2);
    assert(retiredAccounting.logicalBytes == workBytes + frameBytes);

    ID3D12Resource* retainedPassScratch = scratch.Get(D3D12NrScratchKind::PassScratch);
    ID3D12Resource* retainedColorSmall = scratch.Get(D3D12NrScratchKind::ColorSmall);
    ID3D12Resource* retainedOutputNative = scratch.Get(D3D12NrScratchKind::OutputNative);
    ID3D12Resource* retainedResidualEdited = scratch.Get(D3D12NrScratchKind::ResidualEdited);
    assert(scratch.Ensure(gpu.device.Get(), resized, retirement));
    assert(scratch.Matches(resized));
    assert(scratch.Get(D3D12NrScratchKind::PassScratch) == retainedPassScratch);
    assert(scratch.Get(D3D12NrScratchKind::ColorSmall) == retainedColorSmall);
    assert(scratch.Get(D3D12NrScratchKind::OutputNative) == retainedOutputNative);
    assert(scratch.Get(D3D12NrScratchKind::ResidualEdited) == retainedResidualEdited);
    const auto resizedAccounting = scratch.Accounting();
    assert(resizedAccounting.resourceCount == 10);
    assert(resizedAccounting.logicalBytesExact);
    assert(resizedAccounting.logicalBytes ==
           1200ull * 675ull * 8ull + 2ull * 1600ull * 900ull * 8ull +
           640ull * 360ull * 8ull + workBytes + 5ull * frameBytes);
    assert(retirement.Size() == 5);
    retiredAccounting = retirement.ResourceAccounting();
    assert(retiredAccounting.resourceCount == 5);
    assert(retiredAccounting.logicalBytesExact);

    assert(scratch.Retire(retirement));
    assert(!scratch.Complete());
    assert(scratch.Accounting().resourceCount == 0);
    assert(scratch.Accounting().logicalBytes == 0);
    assert(retirement.Size() == 15);
    retiredAccounting = retirement.ResourceAccounting();
    assert(retiredAccounting.resourceCount == 15);
    assert(retiredAccounting.logicalBytesExact);

    D3D12NrScratchResources usageScratch;
    NrDeferredRetirementQueue usageRetirement;
    assert(usageScratch.Ensure(gpu.device.Get(), native, usageRetirement));
    for (const auto kind : {
             D3D12NrScratchKind::PassScratch,
             D3D12NrScratchKind::ColorSmall,
             D3D12NrScratchKind::OutputNative,
             D3D12NrScratchKind::ActiveColor,
             D3D12NrScratchKind::ResidualEdited,
             D3D12NrScratchKind::ResidualHistory0,
             D3D12NrScratchKind::ResidualHistory1,
             D3D12NrScratchKind::ResidualComposed}) {
        assert(usageScratch.EnsureOptional(
            gpu.device.Get(), kind, native.format,
            native.frameWidth, native.frameHeight, usageRetirement));
    }
    D3D12NrScratchUsage passOnly{};
    passOnly.passScratch = true;
    assert(usageScratch.RetireUnused(passOnly, usageRetirement));
    assert(usageScratch.Get(D3D12NrScratchKind::PassScratch) != nullptr);
    assert(usageScratch.Get(D3D12NrScratchKind::ColorSmall) == nullptr);
    assert(usageScratch.Get(D3D12NrScratchKind::ActiveColor) == nullptr);
    assert(usageScratch.Get(D3D12NrScratchKind::ResidualHistory0) == nullptr);
    assert(usageRetirement.Size() == 7);
    const auto passOnlyAccounting = usageScratch.Accounting();
    assert(passOnlyAccounting.resourceCount == 4);
    assert(passOnlyAccounting.logicalBytesExact);
    assert(passOnlyAccounting.logicalBytes ==
           workBytes + 3ull * frameBytes);

    assert(usageScratch.RetireUnused({}, usageRetirement));
    assert(usageScratch.Get(D3D12NrScratchKind::PassScratch) == nullptr);
    assert(usageScratch.Accounting().resourceCount == 3);
    assert(usageScratch.Accounting().logicalBytes ==
           workBytes + 2ull * frameBytes);
    assert(usageRetirement.Size() == 8);
    assert(usageScratch.Retire(usageRetirement));
    assert(usageRetirement.Size() == 11);

    D3D12NrScratchResources reuseScratch;
    NrDeferredRetirementQueue reuseRetirement;
    assert(reuseScratch.Ensure(gpu.device.Get(), native, reuseRetirement));
    ID3D12Resource* originalOutput = reuseScratch.Get(D3D12NrScratchKind::Output);
    ID3D12Resource* originalColor = reuseScratch.Get(D3D12NrScratchKind::ColorCopy);
    ID3D12Resource* originalHdr = reuseScratch.Get(D3D12NrScratchKind::HdrCopy);
    const D3D12NrScratchDesc workOnlyResize{
        native.format, native.frameWidth, native.frameHeight, 960, 540};
    assert(reuseScratch.Ensure(gpu.device.Get(), workOnlyResize, reuseRetirement));
    assert(reuseScratch.Get(D3D12NrScratchKind::Output) != originalOutput);
    assert(reuseScratch.Get(D3D12NrScratchKind::ColorCopy) == originalColor);
    assert(reuseScratch.Get(D3D12NrScratchKind::HdrCopy) == originalHdr);
    assert(reuseRetirement.Size() == 1);
    const auto workOnlyRetired = reuseRetirement.ResourceAccounting();
    assert(workOnlyRetired.resourceCount == 1);
    assert(workOnlyRetired.logicalBytes == workBytes);
    assert(workOnlyRetired.logicalBytesExact);

    assert(SUCCEEDED(gpu.list->Close()));
    unsigned released = 0;
    retirement.DrainAfterIdle(&released, &ReleaseRetired);
    assert(released == 15);
    assert(retirement.Size() == 0);
    assert(retirement.ResourceAccounting().resourceCount == 0);
    usageRetirement.DrainAfterIdle(&released, &ReleaseRetired);
    assert(released == 26);
    assert(usageRetirement.Size() == 0);
    unsigned reuseReleased = 0;
    reuseRetirement.DrainAfterIdle(&reuseReleased, &ReleaseRetired);
    assert(reuseReleased == 1);
    assert(reuseRetirement.Size() == 0);

    scratch.ReleaseAfterIdle();
    usageScratch.ReleaseAfterIdle();
    reuseScratch.ReleaseAfterIdle();
    return 0;
}
