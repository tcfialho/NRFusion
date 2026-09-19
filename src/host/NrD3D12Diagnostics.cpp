#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <dxgi1_6.h>
#include <d3d12.h>
#include "nrfusion/NrD3D12Diagnostics.hpp"
#include "nrfusion/NrDiagnosticsApi.hpp"
#include <wrl/client.h>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace nrfusion {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kQueryCapacity = 32768;
struct KernelSample {
    unsigned query = 0;
    std::string name;
    unsigned count = 0;
    NVAPI_DIM3 grid{}, block{};
    unsigned shared = 0;
    std::vector<unsigned char> parameters;
};
struct DiagnosticState {
    std::recursive_mutex mutex;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    std::unordered_map<NVDX_ObjectHandle, std::string> names;
    std::unordered_map<std::string, std::pair<unsigned, unsigned long long>> launchShapes;
    std::vector<KernelSample> samples;
    std::vector<unsigned> passQueries;
    std::ofstream csv;
    std::string csvPath;
    NrDiagnosticFrame result{};
    unsigned nextQuery = 0;
    bool active = false, profile = false, failed = false;
    unsigned passDepth = 0;
};
DiagnosticState& Shared() {
    // Resources must outlive any command list holding a query reference.
    static auto* state = new DiagnosticState;
    return *state;
}
bool Prepare(DiagnosticState& state, ID3D12Device* device) {
    if (state.device) return state.device.Get() == device;
    D3D12_QUERY_HEAP_DESC queries{};
    queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queries.Count = kQueryCapacity;
    if (FAILED(device->CreateQueryHeap(&queries, IID_PPV_ARGS(&state.queries)))) return false;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = kQueryCapacity * sizeof(UINT64);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&state.readback)))) return false;
    state.device = device;
    state.samples.reserve(kQueryCapacity / 2);
    return true;
}
bool ReserveQuery(DiagnosticState& state, ID3D12GraphicsCommandList* commands, unsigned& query) {
    if (state.nextQuery + 2 > kQueryCapacity) { state.failed = true; return false; }
    query = state.nextQuery;
    state.nextQuery += 2;
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
    return true;
}
void EndQuery(DiagnosticState& state, ID3D12GraphicsCommandList* commands, unsigned query) {
    commands->EndQuery(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
    commands->ResolveQueryData(state.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 2,
                              state.readback.Get(), query * sizeof(UINT64));
}
} // namespace

void NoteNrFunction(NVDX_ObjectHandle function, const char* name) {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    state.names[function] = name ? name : "<unnamed>";
}
std::string NrFunctionNamesSummary() {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    std::vector<std::string> unique;
    for (const auto& entry : state.names)
        if (std::find(unique.begin(), unique.end(), entry.second) == unique.end())
            unique.push_back(entry.second);
    std::sort(unique.begin(), unique.end());
    std::string out;
    for (const auto& name : unique) {
        if (!out.empty()) out += ", ";
        out += name;
    }
    return out;
}

std::string NrLaunchShapeSummary() {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    std::vector<std::pair<unsigned long long, std::string>> rows;
    for (const auto& entry : state.launchShapes)
        rows.push_back({entry.second.second,
                        entry.first + ":p" + std::to_string(entry.second.first) +
                        " x" + std::to_string(entry.second.second)});
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::string out;
    for (size_t i = 0; i < rows.size() && i < 24; ++i) {
        if (!out.empty()) out += ", ";
        out += rows[i].second;
    }
    return out;
}

void ForgetNrFunction(NVDX_ObjectHandle function) {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    state.names.erase(function);
}

NvAPI_Status ProfileNrChain(ID3D12GraphicsCommandList* commands,
    const NVAPI_CU_KERNEL_LAUNCH_PARAMS* kernels, NvU32 count,
    decltype(&NvAPI_D3D12_LaunchCuKernelChain) original) {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    // Recorded before the profiling gate: which kernel carries which parameter block is the only
    // way to tell one neural layer family from another, and it must be known even when idle.
    for (NvU32 index = 0; kernels && index < count; ++index) {
        const auto found = state.names.find(kernels[index].hFunction);
        if (found == state.names.end()) continue;
        auto& entry = state.launchShapes[found->second];
        entry.first = kernels[index].paramSize;
        ++entry.second;
    }
    if (!state.active || !state.passDepth) return original(commands, kernels, count);
    ++state.result.chainCalls;
    state.result.kernelLaunches += count;
    unsigned query = 0;
    const bool timed = state.profile && ReserveQuery(state, commands, query);
    if (timed) {
        KernelSample sample;
        sample.query = query;
        sample.count = count;
        // A multi-kernel chain stays intact. Its aggregate duration is not assigned to a kernel.
        for (NvU32 index = 0; kernels && index < count; ++index) {
            if (index) sample.name += " | ";
            const auto found = state.names.find(kernels[index].hFunction);
            sample.name += found == state.names.end() ? "<unknown>" : found->second;
        }
        if (kernels && count == 1) {
            sample.grid = kernels[0].gridDim;
            sample.block = kernels[0].blockDim;
            sample.shared = kernels[0].dynSharedMemBytes;
            if (kernels[0].pParams && kernels[0].paramSize <= 65536) {
                const auto* parameters = static_cast<const unsigned char*>(kernels[0].pParams);
                sample.parameters.assign(parameters, parameters + kernels[0].paramSize);
            }
        }
        state.samples.push_back(std::move(sample));
    }
    const auto result = original(commands, kernels, count);
    if (timed) EndQuery(state, commands, query);
    if (result != NVAPI_OK) state.failed = true;
    return result;
}

NrPassDiagnosticScope::NrPassDiagnosticScope(ID3D12GraphicsCommandList* commands) {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    if (!state.active) return;
    if (ReserveQuery(state, commands, query_)) {
        commands_ = commands;
        state.passQueries.push_back(query_);
        ++state.result.passes;
        ++state.passDepth;
    }
}
NrPassDiagnosticScope::~NrPassDiagnosticScope() {
    if (!commands_) return;
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    EndQuery(state, commands_, query_);
    --state.passDepth;
    if (successful_) ++state.result.successfulPasses;
}

extern "C" __declspec(dllexport) int NRFusion_BeginNrDiagnosticFrame(
    ID3D12Device* device, std::uint64_t frame, int profile, const char* csvPath) {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    if (!device || state.active || !Prepare(state, device)) return 0;
    if (profile) {
        const std::string requestedPath = csvPath ? csvPath : "";
        if (requestedPath.empty()) return 0;
        if (state.csvPath != requestedPath) {
            state.csv.close();
            state.csv.open(requestedPath);
            if (!state.csv) return 0;
            state.csvPath = requestedPath;
            state.csv << "frame,order,name,chain_count,gpu_ms,grid_x,grid_y,grid_z,block_x,block_y,block_z,shared_bytes,param_bytes,params_hex\n";
        }
    }
    state.active = true;
    state.profile = profile != 0;
    state.failed = false;
    state.nextQuery = state.passDepth = 0;
    state.result = {};
    state.result.frame = frame;
    state.samples.clear();
    state.passQueries.clear();
    return 1;
}

extern "C" __declspec(dllexport) int NRFusion_ReadNrDiagnosticFrame(
    ID3D12CommandQueue* queue, ID3D12Fence* fence, std::uint64_t completion, NrDiagnosticFrame* output) {
    auto& state = Shared();
    std::lock_guard guard(state.mutex);
    if (!state.active || !queue || !fence || !output || state.passDepth ||
        fence->GetCompletedValue() < completion || FAILED(state.device->GetDeviceRemovedReason())) return 0;
    state.active = false;
    UINT64 frequency = 0;
    if (state.failed || FAILED(queue->GetTimestampFrequency(&frequency)) || !frequency) return 0;
    UINT64* ticks = nullptr;
    D3D12_RANGE read{0, state.nextQuery * sizeof(UINT64)};
    if (FAILED(state.readback->Map(0, &read, reinterpret_cast<void**>(&ticks)))) return 0;
    auto milliseconds = [&](unsigned query) {
        if (ticks[query + 1] <= ticks[query]) { state.failed = true; return 0.0; }
        return double(ticks[query + 1] - ticks[query]) * 1000.0 / double(frequency);
    };
    for (unsigned query : state.passQueries) state.result.nrGpuMs += milliseconds(query);
    for (std::size_t order = 0; order < state.samples.size(); ++order) {
        const auto& sample = state.samples[order];
        state.csv << state.result.frame << ',' << order << ',' << std::quoted(sample.name) << ','
            << sample.count << ',' << milliseconds(sample.query) << ','
            << sample.grid.x << ',' << sample.grid.y << ',' << sample.grid.z << ','
            << sample.block.x << ',' << sample.block.y << ',' << sample.block.z << ','
            << sample.shared << ',' << sample.parameters.size() << ',';
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned char byte : sample.parameters) state.csv << hex[byte >> 4] << hex[byte & 15];
        state.csv << '\n';
    }
    D3D12_RANGE written{0, 0};
    state.readback->Unmap(0, &written);
    if (state.profile) { state.csv.flush(); if (!state.csv) state.failed = true; }
    *output = state.result;
    return state.failed ? 0 : 1;
}
} // namespace nrfusion
