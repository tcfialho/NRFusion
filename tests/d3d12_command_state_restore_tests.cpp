#include "nrfusion/D3D12CommandStateRestore.hpp"

#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;
using namespace nrfusion;

namespace {

void Check(HRESULT hr) {
    assert(SUCCEEDED(hr));
}

ComPtr<ID3D12RootSignature> MakeRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = &range;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 1;
    desc.pParameters = &parameter;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    Check(D3D12SerializeRootSignature(
        &desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors));

    ComPtr<ID3D12RootSignature> root;
    Check(device->CreateRootSignature(
        0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&root)));
    return root;
}

ComPtr<ID3D12PipelineState> MakePipeline(
    ID3D12Device* device, ID3D12RootSignature* root, std::uint32_t value) {
    const char* source42 =
        "RWStructuredBuffer<uint> outBuffer : register(u0);"
        "[numthreads(1,1,1)] void main(){outBuffer[0]=42;}";
    const char* source7 =
        "RWStructuredBuffer<uint> outBuffer : register(u0);"
        "[numthreads(1,1,1)] void main(){outBuffer[0]=7;}";
    const char* source = value == 42 ? source42 : source7;

    ComPtr<ID3DBlob> shader;
    ComPtr<ID3DBlob> errors;
    Check(D3DCompile(
        source, std::strlen(source), nullptr, nullptr, nullptr,
        "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &errors));

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root;
    desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};

    ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)));
    return pipeline;
}

ComPtr<ID3D12Resource> MakeBuffer(
    ID3D12Device* device, D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState, D3D12_RESOURCE_FLAGS flags) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(std::uint32_t);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    Check(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr,
        IID_PPV_ARGS(&resource)));
    return resource;
}

ComPtr<ID3D12DescriptorHeap> MakeUavHeap(
    ID3D12Device* device, ID3D12Resource* resource) {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ComPtr<ID3D12DescriptorHeap> heap;
    Check(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_UNKNOWN;
    uav.Buffer.NumElements = 1;
    uav.Buffer.StructureByteStride = sizeof(std::uint32_t);
    device->CreateUnorderedAccessView(
        resource, nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
    return heap;
}

struct RootTableState {
    D3D12_GPU_DESCRIPTOR_HANDLE table{};
};

void RestoreRootTable(
    ID3D12GraphicsCommandList* commandList, void* context) noexcept {
    const auto* state = static_cast<const RootTableState*>(context);
    commandList->SetComputeRootDescriptorTable(0, state->table);
}

} // namespace

int main() {
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter> warp;
    Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)));

    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(
        warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));

    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> list;
    Check(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&list)));

    auto expectedRoot = MakeRootSignature(device.Get());
    auto clobberRoot = MakeRootSignature(device.Get());
    auto expectedPso = MakePipeline(device.Get(), expectedRoot.Get(), 42);
    auto clobberPso = MakePipeline(device.Get(), clobberRoot.Get(), 7);
    auto expectedBuffer = MakeBuffer(
        device.Get(), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto clobberBuffer = MakeBuffer(
        device.Get(), D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto readback = MakeBuffer(
        device.Get(), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE);
    auto expectedHeap = MakeUavHeap(device.Get(), expectedBuffer.Get());
    auto clobberHeap = MakeUavHeap(device.Get(), clobberBuffer.Get());

    ID3D12DescriptorHeap* clobberHeaps[] = {clobberHeap.Get()};
    list->SetDescriptorHeaps(1, clobberHeaps);
    list->SetComputeRootSignature(clobberRoot.Get());
    list->SetPipelineState(clobberPso.Get());
    list->SetComputeRootDescriptorTable(
        0, clobberHeap->GetGPUDescriptorHandleForHeapStart());

    RootTableState rootTable{
        expectedHeap->GetGPUDescriptorHandleForHeapStart()};
    D3D12CommandStateRestore restore{};
    restore.restoreDescriptorHeaps = true;
    restore.descriptorHeapCount = 1;
    restore.descriptorHeaps[0] = expectedHeap.Get();
    restore.restoreComputeRootSignature = true;
    restore.computeRootSignature = expectedRoot.Get();
    restore.restorePipelineState = true;
    restore.pipelineState = expectedPso.Get();
    restore.restoreAdditionalState = RestoreRootTable;
    restore.additionalStateContext = &rootTable;

    D3D12CommandStateRestore invalid = restore;
    invalid.descriptorHeapCount = 3;
    assert(!RestoreD3D12CommandState(list.Get(), &invalid));

    constexpr int defaultIterations = 250000;
    const auto defaultStart = std::chrono::steady_clock::now();
    for (int i = 0; i < defaultIterations; ++i)
        assert(RestoreD3D12CommandState(list.Get(), nullptr));
    const auto defaultEnd = std::chrono::steady_clock::now();

    constexpr int compatibilityIterations = 10000;
    const auto compatibilityStart = std::chrono::steady_clock::now();
    for (int i = 0; i < compatibilityIterations; ++i)
        assert(RestoreD3D12CommandState(list.Get(), &restore));
    const auto compatibilityEnd = std::chrono::steady_clock::now();

    const auto defaultNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        defaultEnd - defaultStart).count() / defaultIterations;
    const auto compatibilityNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            compatibilityEnd - compatibilityStart).count() /
        compatibilityIterations;
    std::printf(
        "D3D12 state restore: default=%lld ns/call, compatibility=%lld ns/call\n",
        static_cast<long long>(defaultNs),
        static_cast<long long>(compatibilityNs));

    assert(RestoreD3D12CommandState(list.Get(), &restore));
    list->Dispatch(1, 1, 1);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = expectedBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    list->CopyBufferRegion(
        readback.Get(), 0, expectedBuffer.Get(), 0, sizeof(std::uint32_t));
    Check(list->Close());

    ID3D12CommandList* lists[] = {list.Get()};
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    Check(queue->Signal(fence.Get(), 1));
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(event != nullptr);
    Check(fence->SetEventOnCompletion(1, event));
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);

    std::uint32_t* mapped = nullptr;
    D3D12_RANGE readRange{0, sizeof(std::uint32_t)};
    Check(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)));
    assert(*mapped == 42);
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    return 0;
}
