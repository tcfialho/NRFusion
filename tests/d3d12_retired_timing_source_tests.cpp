#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "D3D12TestDevice.hpp"
#include "nrfusion/D3D12RetiredTimingSource.hpp"
#include "nrfusion/D3D12CarrierNativeFacts.hpp"
#include "nrfusion/D3D12CarrierSession.hpp"

#include <cassert>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {

nrfusion::WorkTicket Ticket(std::uint64_t id) {
    return {id, 2, 100 + id, id, 7, 1.0f, 8};
}





ComPtr<ID3D12Resource> Buffer(
    ID3D12Device* device, D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES state, UINT64 size) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> resource;
    assert(SUCCEEDED(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
        IID_PPV_ARGS(&resource))));
    return resource;
}

nrfusion::D3D12CarrierFramePacket Packet(
    std::uint64_t generation, nrfusion::FrameId frameId) {
    using namespace nrfusion;
    D3D12NativeAcquireInput input{};
    input.identity.frameId = frameId;
    input.identity.configurationGeneration = generation;
    input.color.texture = {11, {1920, 1080}, ResourceFormat::Rgba16Float, 1, 1, 1, true};
    input.color.provenance = ResourceProvenance::GameNative;
    input.color.reliability = ResourceReliability::Reliable;
    input.output = {12, {1920, 1080}, ResourceFormat::Unknown, 1, 1, 1, true};
    const auto native = BuildD3D12NativeAcquireSnapshot(input);
    assert(native);

    D3D12CarrierFramePacket packet{};
    packet.game.api = GraphicsApi::D3D12;
    packet.acquire = native.snapshot;
    packet.capabilities.syntheticD3D12 = true;
    packet.capabilities.postSr = true;
    packet.capabilities.fp8 = true;
    packet.telemetry.dtSeconds = 1.0 / 60.0;
    packet.telemetry.nrTimingFresh = false;
    return packet;
}

void WaitFor(ID3D12Fence* fence, std::uint64_t value) {
    HANDLE eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    assert(eventHandle != nullptr);
    assert(SUCCEEDED(fence->SetEventOnCompletion(value, eventHandle)));
    assert(WaitForSingleObject(eventHandle, 5000) == WAIT_OBJECT_0);
    CloseHandle(eventHandle);
}

}

int main() {
    using namespace nrfusion;
    if (!nrfusion::testing::UseHardwareD3D12TestDevice()) return 77;

    ComPtr<ID3D12Device> device =
        nrfusion::testing::CreateD3D12TestDevice();
    assert(device);

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    assert(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
    assert(SUCCEEDED(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    assert(SUCCEEDED(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
        IID_PPV_ARGS(&commands))));
    assert(SUCCEEDED(device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));

    D3D12RetiredTimingSource source;
    assert(source.BindAfterIdle(device.Get(), queue.Get()));
    assert(source.TimestampFrequency() != 0);

    for (std::uint64_t i = 1; i <= D3D12RetiredTimingSource::kCapacity; ++i) {
        assert(source.Begin(commands.Get(), Ticket(i)));
        assert(source.End(commands.Get(), i));
    }
    assert(source.Size() == D3D12RetiredTimingSource::kCapacity);
    assert(!source.Begin(commands.Get(), Ticket(99)));
    assert(!source.TryRetire(0));

    assert(SUCCEEDED(commands->Close()));
    ID3D12CommandList* lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    constexpr std::uint64_t completion =
        D3D12RetiredTimingSource::kCapacity;
    assert(SUCCEEDED(queue->Signal(fence.Get(), completion)));
    WaitFor(fence.Get(), completion);

    for (std::uint64_t i = 1; i <= completion; ++i) {
        const auto sample = source.TryRetire(fence->GetCompletedValue());
        assert(sample && sample->mapsWork);
        assert(sample->ticket == Ticket(i));
        assert(std::isfinite(sample->gpuMs) && sample->gpuMs >= 0.0);
    }
    assert(source.Size() == 0);

    assert(SUCCEEDED(allocator->Reset()));
    assert(SUCCEEDED(commands->Reset(allocator.Get(), nullptr)));
    assert(source.Begin(commands.Get(), Ticket(99)));
    assert(source.EndInvalid(commands.Get(), completion + 1));
    assert(SUCCEEDED(commands->Close()));
    queue->ExecuteCommandLists(1, lists);
    assert(SUCCEEDED(queue->Signal(fence.Get(), completion + 1)));
    WaitFor(fence.Get(), completion + 1);
    const auto invalid = source.TryRetire(fence->GetCompletedValue());
    assert(invalid && !invalid->mapsWork);

    source.ResetAfterIdle();
    assert(source.Size() == 0);
    assert(source.TimestampFrequency() == 0);
    assert(SUCCEEDED(allocator->Reset()));
    assert(SUCCEEDED(commands->Reset(allocator.Get(), nullptr)));

    constexpr UINT64 kCopyBytes = 1024 * 1024;
    auto copySource = Buffer(
        device.Get(), D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ, kCopyBytes);
    auto copyTarget = Buffer(
        device.Get(), D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST, kCopyBytes);

    D3D12RetiredTimingSource integratedSource;
    assert(integratedSource.BindAfterIdle(device.Get(), queue.Get()));
    RuntimeConfig config{};
    config.generation = 50;
    config.enabled = true;
    config.targetFps = 60.0f;
    PerformanceConfig performance{};
    performance.targetFps = 60.0;
    performance.nrWarmupSamples = 2;
    D3D12CarrierSession carrier;
    assert(carrier.Configure(config, performance));

    auto integratedFrame = carrier.Resolve(Packet(config.generation, 1000));
    assert(integratedFrame);
    for (std::uint64_t sampleIndex = 0; sampleIndex < 2; ++sampleIndex) {
        const auto work = carrier.BeginWork(integratedFrame, sampleIndex + 1);
        assert(work && carrier.ClaimExecuteWork(*work));
        assert(integratedSource.Begin(commands.Get(), work->ticket));
        commands->CopyBufferRegion(
            copyTarget.Get(), 0, copySource.Get(), 0, kCopyBytes);
        assert(carrier.SubmitWork(*work));
        assert(carrier.MapTimedWork(*work));

        const std::uint64_t value = 100 + sampleIndex;
        assert(integratedSource.End(commands.Get(), value));
        assert(SUCCEEDED(commands->Close()));
        queue->ExecuteCommandLists(1, lists);
        assert(SUCCEEDED(queue->Signal(fence.Get(), value)));
        WaitFor(fence.Get(), value);

        const auto sample = integratedSource.TryRetire(fence->GetCompletedValue());
        assert(sample && sample->ticket == work->ticket);
        assert(carrier.RetireTimedSample(*sample));

        assert(SUCCEEDED(allocator->Reset()));
        assert(SUCCEEDED(commands->Reset(allocator.Get(), nullptr)));
        integratedFrame = carrier.Resolve(
            Packet(config.generation, 1001 + sampleIndex));
        assert(integratedFrame);
        assert(integratedFrame.session.decision.performance.telemetryReady ==
               (sampleIndex == 1));
    }

    const auto noReplay = carrier.Resolve(Packet(config.generation, 1003));
    assert(noReplay);
    assert(!noReplay.session.decision.performance.telemetryReady);
    integratedSource.ResetAfterIdle();
    return 0;
}
