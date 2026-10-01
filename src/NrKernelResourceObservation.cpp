#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <MinHook.h>
#include "NrKernelResourceObservation.hpp"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

extern "C" void NRFusion_ResourceCreationTargets(ID3D12Device*, void**);
extern "C" void* NRFusion_ResourceBarrierTarget(ID3D12GraphicsCommandList*);

namespace nrfusion::kernelprofile {
namespace {
using Microsoft::WRL::ComPtr;
using Committed = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_HEAP_PROPERTIES*,
    D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
using Placed = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Heap*, UINT64,
    const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
using Reserved = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_RESOURCE_DESC*,
    D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**);
using Barriers = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, const D3D12_RESOURCE_BARRIER*);

struct TrackedBuffer {
    ComPtr<ID3D12Resource> resource;
    ID3D12Device* device = nullptr;
    BufferObservation observation{};
};
struct ResourceState {
    std::mutex mutex;
    std::vector<TrackedBuffer> buffers;
    std::array<void*, 3> targets{};
    std::atomic<bool> installed{false};
    Committed committed = nullptr;
    Placed placed = nullptr;
    Reserved reserved = nullptr;
    std::uint64_t nextId = 0;
    Barriers barriers = nullptr;
    void* barrierTarget = nullptr;
};

ResourceState& Resources() {
    static auto* state = new ResourceState;
    return *state;
}

void STDMETHODCALLTYPE TrackBarriers(ID3D12GraphicsCommandList* commands, UINT count,
                                      const D3D12_RESOURCE_BARRIER* barriers) {
    auto& state = Resources();
    state.barriers(commands, count, barriers);
    std::lock_guard lock(state.mutex);
    for (UINT index = 0; index < count; ++index) {
        const auto& barrier = barriers[index];
        if (barrier.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) continue;
        for (auto& buffer : state.buffers)
            if (buffer.resource.Get() == barrier.Transition.pResource)
                buffer.observation.currentState = barrier.Transition.StateAfter;
    }
}

void RecordBuffer(void** output, D3D12_RESOURCE_STATES initial) {
    if (!output || !*output) return;
    ComPtr<ID3D12Resource> resource;
    if (FAILED(static_cast<IUnknown*>(*output)->QueryInterface(IID_PPV_ARGS(&resource)))) return;
    const auto desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER) return;
    const auto address = resource->GetGPUVirtualAddress();
    if (!address) return;
    auto& state = Resources();
    std::lock_guard lock(state.mutex);
    BufferObservation observation{};
    observation.allocationId = ++state.nextId;
    observation.base = address;
    observation.bytes = desc.Width;
    observation.initialState = initial;
    observation.currentState = initial;
    observation.resourceId = reinterpret_cast<std::uintptr_t>(resource.Get());
    observation.matched = true;
    ComPtr<ID3D12Device> owner;
    if (FAILED(resource->GetDevice(IID_PPV_ARGS(&owner)))) return;
    state.buffers.push_back({std::move(resource), owner.Get(), observation});
}

HRESULT STDMETHODCALLTYPE ObserveCommitted(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heap,
    D3D12_HEAP_FLAGS flags, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial,
    const D3D12_CLEAR_VALUE* clear, REFIID iid, void** output) {
    const auto result = Resources().committed(device, heap, flags, desc, initial, clear, iid, output);
    if (SUCCEEDED(result)) RecordBuffer(output, initial);
    return result;
}

HRESULT STDMETHODCALLTYPE ObservePlaced(ID3D12Device* device, ID3D12Heap* heap, UINT64 offset,
    const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial,
    const D3D12_CLEAR_VALUE* clear, REFIID iid, void** output) {
    const auto result = Resources().placed(device, heap, offset, desc, initial, clear, iid, output);
    if (SUCCEEDED(result)) RecordBuffer(output, initial);
    return result;
}

HRESULT STDMETHODCALLTYPE ObserveReserved(ID3D12Device* device, const D3D12_RESOURCE_DESC* desc,
    D3D12_RESOURCE_STATES initial, const D3D12_CLEAR_VALUE* clear, REFIID iid, void** output) {
    const auto result = Resources().reserved(device, desc, initial, clear, iid, output);
    if (SUCCEEDED(result)) RecordBuffer(output, initial);
    return result;
}

} // namespace

bool StartResourceObservation(ID3D12Device* device) {
    wchar_t enabled[2]{};
    if (!device || !GetEnvironmentVariableW(L"NRFUSION_KERNEL_ABI", enabled, 2)) return false;
    auto& state = Resources();
    std::lock_guard lock(state.mutex);
    if (state.installed.load()) return true;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    NRFusion_ResourceCreationTargets(device, state.targets.data());
    void* observers[]{reinterpret_cast<void*>(&ObserveCommitted), reinterpret_cast<void*>(&ObservePlaced),
                      reinterpret_cast<void*>(&ObserveReserved)};
    void** originals[]{reinterpret_cast<void**>(&state.committed), reinterpret_cast<void**>(&state.placed),
                       reinterpret_cast<void**>(&state.reserved)};
    unsigned created = 0;
    for (; created < state.targets.size(); ++created) {
        if (MH_CreateHook(state.targets[created], observers[created], originals[created]) != MH_OK) break;
    }
    if (created != state.targets.size()) {
        while (created) MH_RemoveHook(state.targets[--created]);
        return false;
    }
    for (void* target : state.targets) {
        if (MH_QueueEnableHook(target) != MH_OK) return false;
    }
    if (MH_ApplyQueued() != MH_OK) return false;
    state.installed.store(true);
    return true;
}

void ResetObservedResources(ID3D12Device* device) {
    auto& state = Resources();
    if (!state.installed.load()) return;
    std::lock_guard lock(state.mutex);
    std::erase_if(state.buffers, [device](const TrackedBuffer& buffer) { return buffer.device == device; });
}

BufferObservation ObserveD3D12Buffer(std::uint64_t candidate) noexcept {
    auto& state = Resources();
    std::lock_guard lock(state.mutex);
    for (const auto& buffer : state.buffers) {
        const auto& range = buffer.observation;
        if (candidate >= range.base && candidate - range.base < range.bytes) {
            auto observed = range;
            observed.offset = candidate - range.base;
            return observed;
        }
    }
    return {};
}

bool ObserveCommandBarriers(ID3D12GraphicsCommandList* commands) {
    auto& state = Resources();
    if (!commands || !state.installed.load()) return false;
    std::lock_guard lock(state.mutex);
    if (state.barrierTarget) return true;
    void* target = NRFusion_ResourceBarrierTarget(commands);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&TrackBarriers),
            reinterpret_cast<void**>(&state.barriers)) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        return false;
    }
    state.barrierTarget = target;
    return true;
}

ID3D12Resource* RetainObservedBuffer(std::uint64_t address, std::uint64_t bytes,
                                    std::uint64_t& offset, std::uint32_t& current) {
    auto& state = Resources();
    std::lock_guard lock(state.mutex);
    for (const auto& buffer : state.buffers) {
        const auto& range = buffer.observation;
        if (address < range.base || address - range.base >= range.bytes) continue;
        offset = address - range.base;
        if (bytes > range.bytes - offset) return nullptr;
        current = range.currentState;
        buffer.resource->AddRef();
        return buffer.resource.Get();
    }
    return nullptr;
}

} // namespace nrfusion::kernelprofile
