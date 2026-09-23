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

#include <cassert>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {

nrfusion::WorkTicket Ticket(std::uint64_t id) {
    return {id, 2, 100 + id, id, 7, 1.0f, 8};
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
    return 0;
}
