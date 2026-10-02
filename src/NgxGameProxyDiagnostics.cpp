#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "NgxGameProxyDiagnostics.hpp"
#include "nrfusion/NrDiagnosticsApi.hpp"
#include "NrKernelProfileD3D12.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace {
using Microsoft::WRL::ComPtr;

struct DiagnosticState {
    std::mutex mutex;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    nrfusion::NrDiagnosticFrame result{};
    nrfusion::NrDiagnosticStages stages{};
    unsigned stageMask = 0;
    bool retired = false;
    bool active = false;
    bool passOpen = false;
};

DiagnosticState& State() {
    static auto* state = new DiagnosticState;
    return *state;
}
std::atomic<bool> g_active{false};

bool EnsureResources(DiagnosticState& state, ID3D12Device* device) {
    if (state.device.Get() == device && state.queries && state.readback) return true;
    state.device.Reset();
    state.queries.Reset();
    state.readback.Reset();

    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = 4;
    if (FAILED(device->CreateQueryHeap(&query, IID_PPV_ARGS(&state.queries))))
        return false;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 4 * sizeof(std::uint64_t);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&state.readback))))
        return false;
    state.device = device;
    return true;
}
} // namespace

namespace nrfusion::ngxproxy {

void RecordDiagnosticGpuStage(ID3D12GraphicsCommandList* commands, NrGpuStage marker) noexcept {
    const auto stage = static_cast<unsigned>(marker);
    if (!g_active.load(std::memory_order_relaxed) || !commands || stage < 2 || stage > 3) return;
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!state.active || !state.passOpen) return;
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, stage);
    state.stageMask |= 1u << (stage - 2);
}

RecordNrGpuStage DiagnosticGpuStageRecorder() noexcept {
    return g_active.load(std::memory_order_relaxed) ? RecordDiagnosticGpuStage : nullptr;
}

void BeginDiagnosticPass(ID3D12GraphicsCommandList* commands) noexcept {
    if (!g_active.load(std::memory_order_relaxed) || !commands) return;
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!state.active || state.passOpen) return;
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    state.passOpen = true;
    state.result.passes = 1;
    nrfusion::kernelprofile::BeginNeuralPass(commands);
}

void EndDiagnosticPass(
    ID3D12GraphicsCommandList* commands, bool successful) noexcept {
    if (!g_active.load(std::memory_order_relaxed) || !commands) return;
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!state.active || !state.passOpen) return;
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    commands->ResolveQueryData(
        state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, state.stageMask == 3 ? 4 : 2,
        state.readback.Get(), 0);
    state.passOpen = false;
    state.result.successfulPasses = successful ? 1 : 0;
    nrfusion::kernelprofile::EndNeuralPass();
}

} // namespace nrfusion::ngxproxy
extern "C" __declspec(dllexport) int NRFusion_BeginNrDiagnosticFrame(
    ID3D12Device* device, std::uint64_t frame, int profile, const char* csvPath) {
    if (!device) return 0;
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (state.active || !EnsureResources(state, device)) return 0;
    if (profile && !nrfusion::kernelprofile::BeginFrame(device, frame, csvPath)) return 0;
    state.result = {};
    state.result.frame = frame;
    state.stages = {};
    state.stages.frame = frame;
    state.stageMask = 0;
    state.retired = false;
    state.active = true;
    state.passOpen = false;
    g_active.store(true, std::memory_order_relaxed);
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_ReadNrDiagnosticFrame(
    ID3D12CommandQueue* queue, ID3D12Fence* fence, std::uint64_t completion,
    nrfusion::NrDiagnosticFrame* output) {
    if (!queue || !fence || !output || fence->GetCompletedValue() < completion) return 0;
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!state.active || state.passOpen) return 0;

    UINT64 frequency = 0;
    if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0) return 0;
    std::uint64_t* ticks = nullptr;
    D3D12_RANGE read{0, 4 * sizeof(std::uint64_t)};
    if (FAILED(state.readback->Map(0, &read, reinterpret_cast<void**>(&ticks)))) return 0;
    if (state.result.passes != 0 && ticks[1] > ticks[0]) {
        state.result.nrGpuMs =
            static_cast<double>(ticks[1] - ticks[0]) * 1000.0 /
            static_cast<double>(frequency);
    }
    if (state.result.successfulPasses && state.stageMask == 3 &&
        ticks[0] <= ticks[2] && ticks[2] <= ticks[3] && ticks[3] <= ticks[1]) {
        const double scale = 1000.0 / static_cast<double>(frequency);
        state.stages.valid = 1;
        state.stages.preparationGpuMs = static_cast<double>(ticks[2] - ticks[0]) * scale;
        state.stages.modelGpuMs = static_cast<double>(ticks[3] - ticks[2]) * scale;
        state.stages.compositionGpuMs = static_cast<double>(ticks[1] - ticks[3]) * scale;
    }
    D3D12_RANGE written{0, 0};
    state.readback->Unmap(0, &written);
    if (!nrfusion::kernelprofile::RetireFrame(queue, fence, completion,
            state.result.kernelLaunches, state.result.chainCalls)) return 0;
    *output = state.result;
    state.active = false;
    state.retired = true;
    g_active.store(false, std::memory_order_relaxed);
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_ReadNrDiagnosticStages(
    std::uint64_t frame, std::uint32_t bytes, nrfusion::NrDiagnosticStages* output) {
    if (!output || bytes != sizeof(*output)) return 0;
    auto& state = State();
    std::lock_guard guard(state.mutex);
    if (!state.retired || state.stages.frame != frame) return 0;
    if (state.result.successfulPasses && !state.stages.valid) return 0;
    *output = state.stages;
    return 1;
}
