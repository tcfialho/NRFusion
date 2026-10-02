#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelProfileD3D12.hpp"
#include "NrKernelProfileStatistics.hpp"
#include "NrKernelAbiDiscovery.hpp"
#include "NrKernelCapture.hpp"
#include "NrKernelChainCapture.hpp"
#include "NrSwinKernelCapture.hpp"

#include <wrl/client.h>
#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace nrfusion::kernelprofile {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kCapacity = 8192;

struct FrameState {
    std::mutex mutex;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    std::vector<LaunchRecord> records;
    std::array<unsigned, kCapacity> bases{}, counts{};
    ID3D12GraphicsCommandList* commands = nullptr;
    std::string csvPath;
    std::uint64_t frame = 0, dropped = 0, chainCalls = 0, kernelCalls = 0;
    unsigned queryCount = 0, recordCount = 0;
    bool active = false;
};

FrameState& Frame() {
    // Submitted command lists may still reference the queries at process shutdown.
    static auto* state = new FrameState;
    return *state;
}

bool Prepare(FrameState& state, ID3D12Device* device) {
    if (state.device.Get() == device && state.queries && state.readback) return true;
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 2 * kCapacity;
    ComPtr<ID3D12QueryHeap> queries;
    if (FAILED(device->CreateQueryHeap(&desc, IID_PPV_ARGS(&queries)))) return false;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 2 * kCapacity * sizeof(UINT64);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
    state.records.resize(kCapacity);
    state.device = device;
    state.queries = std::move(queries);
    state.readback = std::move(readback);
    return true;
}

} // namespace

bool BeginFrame(ID3D12Device* device, std::uint64_t frame, const char* csvPath) {
    if (!device || !csvPath || !*csvPath || !NvapiObservationEnabled()) return false;
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    if (state.active || !Prepare(state, device) || !PrepareCaptureFrame(device)) return false;
    state.frame = frame;
    state.csvPath = csvPath;
    state.commands = nullptr;
    state.dropped = state.chainCalls = state.kernelCalls = 0;
    state.queryCount = state.recordCount = 0;
    state.active = true;
    return true;
}

void BeginNeuralPass(ID3D12GraphicsCommandList* commands) noexcept {
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    if (state.active) state.commands = commands;
}

void EndNeuralPass() noexcept {
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    state.commands = nullptr;
}

unsigned BeginChain(ID3D12GraphicsCommandList* commands, unsigned count) noexcept {
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    if (!state.active || !commands || state.commands != commands) return UINT32_MAX;
    ++state.chainCalls;
    state.kernelCalls += count;
    if (!count || count > kCapacity - state.recordCount) {
        state.dropped += count;
        return UINT32_MAX;
    }
    const unsigned query = state.queryCount;
    state.queryCount += 2;
    state.bases[query / 2] = state.recordCount;
    state.counts[query / 2] = count;
    state.recordCount += count;
    return query;
}

void StartChainTimer(ID3D12GraphicsCommandList* commands, unsigned query) noexcept {
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
}

void ObserveLaunch(unsigned query, const LaunchRecord& observation, const void* parameters) noexcept {
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    if (!state.active || query >= state.queryCount ||
        observation.chainIndex >= state.counts[query / 2]) return;
    const unsigned index = state.bases[query / 2] + observation.chainIndex;
    state.records[index] = observation;
    state.records[index].frame = state.frame;
    state.records[index].sequence = index;
    ObservePackedArguments(state.records[index], parameters);
    CaptureSwinBefore(reinterpret_cast<ID3D12GraphicsCommandList*>(observation.commands), state.records[index], parameters);
    CaptureChainBefore(reinterpret_cast<ID3D12GraphicsCommandList*>(observation.commands), state.records[index], parameters);
    CaptureBefore(reinterpret_cast<ID3D12GraphicsCommandList*>(observation.commands), state.records[index], parameters);
}

void EndChain(ID3D12GraphicsCommandList* commands, unsigned query, bool successful) noexcept {
    auto& state = Frame();
    std::lock_guard lock(state.mutex);
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
    commands->ResolveQueryData(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 2,
                              state.readback.Get(), query * sizeof(UINT64));
    for (unsigned index = 0; index < state.counts[query / 2]; ++index)
        state.records[state.bases[query / 2] + index].successful = successful;
}

bool RetireFrame(ID3D12CommandQueue* queue, ID3D12Fence* fence, std::uint64_t completion,
                 std::uint64_t& kernels, std::uint64_t& chains) {
    auto& state = Frame();
    std::vector<LaunchRecord> records;
    std::vector<UINT64> ticks;
    std::string path;
    UINT64 frequency = 0;
    std::uint64_t dropped = 0;
    {
        std::lock_guard lock(state.mutex);
        if (!state.active) return true;
        if (!queue || !fence || state.commands || fence->GetCompletedValue() < completion ||
            FAILED(state.device->GetDeviceRemovedReason()) ||
            FAILED(queue->GetTimestampFrequency(&frequency)) || !frequency) return false;
        ticks.resize(state.queryCount);
        records.assign(state.records.begin(), state.records.begin() + state.recordCount);
        if (state.queryCount) {
            UINT64* mapped = nullptr;
            D3D12_RANGE read{0, state.queryCount * sizeof(UINT64)};
            if (FAILED(state.readback->Map(0, &read,
                    reinterpret_cast<void**>(&mapped)))) return false;
            std::copy_n(mapped, state.queryCount, ticks.data());
            D3D12_RANGE written{0, 0};
            state.readback->Unmap(0, &written);
        }
        kernels = state.kernelCalls;
        chains = state.chainCalls;
        dropped = state.dropped;
        path = state.csvPath;
        state.active = false;
    }
    return RetireCaptures(queue) && WriteFrame(path.c_str(), records.data(), records.size(), ticks.data(), frequency,
                      reinterpret_cast<std::uintptr_t>(queue), dropped);
}

} // namespace nrfusion::kernelprofile
