#include "nrfusion/GameD3D12CommandState.hpp"
#include "nrfusion/Logger.hpp"

#include <MinHook.h>
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace nrfusion {
namespace {

using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using SetPipelineStateFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using SetDescriptorHeapsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, ID3D12DescriptorHeap* const*);
using SetRootSignatureFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
using SetRootDescriptorTableFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_DESCRIPTOR_HANDLE);
using SetRoot32BitConstantFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, UINT);
using SetRoot32BitConstantsFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, UINT, const void*, UINT);
using SetRootViewFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, UINT, D3D12_GPU_VIRTUAL_ADDRESS);

constexpr int kReset = 10;
constexpr int kSetPipelineState = 25;
constexpr int kSetDescriptorHeaps = 28;
constexpr int kSetComputeRootSignature = 29;
constexpr int kSetGraphicsRootSignature = 30;
constexpr int kSetComputeRootDescriptorTable = 31;
constexpr int kSetGraphicsRootDescriptorTable = 32;
constexpr int kSetComputeRoot32BitConstant = 33;
constexpr int kSetGraphicsRoot32BitConstant = 34;
constexpr int kSetComputeRoot32BitConstants = 35;
constexpr int kSetGraphicsRoot32BitConstants = 36;
constexpr int kSetComputeRootConstantBufferView = 37;
constexpr int kSetGraphicsRootConstantBufferView = 38;
constexpr int kSetComputeRootShaderResourceView = 39;
constexpr int kSetGraphicsRootShaderResourceView = 40;
constexpr int kSetComputeRootUnorderedAccessView = 41;
constexpr int kSetGraphicsRootUnorderedAccessView = 42;

ResetFn g_reset = nullptr;
SetPipelineStateFn g_setPipelineState = nullptr;
SetDescriptorHeapsFn g_setDescriptorHeaps = nullptr;
SetRootSignatureFn g_setComputeRootSignature = nullptr;
SetRootSignatureFn g_setGraphicsRootSignature = nullptr;
SetRootDescriptorTableFn g_setComputeRootDescriptorTable = nullptr;
SetRootDescriptorTableFn g_setGraphicsRootDescriptorTable = nullptr;
SetRoot32BitConstantFn g_setComputeRoot32BitConstant = nullptr;
SetRoot32BitConstantFn g_setGraphicsRoot32BitConstant = nullptr;
SetRoot32BitConstantsFn g_setComputeRoot32BitConstants = nullptr;
SetRoot32BitConstantsFn g_setGraphicsRoot32BitConstants = nullptr;
SetRootViewFn g_setComputeRootConstantBufferView = nullptr;
SetRootViewFn g_setGraphicsRootConstantBufferView = nullptr;
SetRootViewFn g_setComputeRootShaderResourceView = nullptr;
SetRootViewFn g_setGraphicsRootShaderResourceView = nullptr;
SetRootViewFn g_setComputeRootUnorderedAccessView = nullptr;
SetRootViewFn g_setGraphicsRootUnorderedAccessView = nullptr;

std::mutex g_installMutex;
bool g_installed = false;
std::mutex g_stateMutex;
std::unordered_map<ID3D12GraphicsCommandList*, GameD3D12CommandStateSnapshot> g_states;
thread_local int g_suppressionDepth = 0;

bool Recording() noexcept { return g_suppressionDepth == 0; }

GameD3D12CommandStateSnapshot& State(ID3D12GraphicsCommandList* list) {
    return g_states[list];
}

void BindRoot(GameD3D12RootSnapshot& root, ID3D12RootSignature* signature) {
    if (root.signature == signature) return;
    root.signature = signature;
    root.args = {};
}

GameD3D12RootArgSnapshot* RootArg(GameD3D12RootSnapshot& root, UINT index) {
    if (root.signature == nullptr || index >= root.args.size()) return nullptr;
    return &root.args[index];
}

void RecordTable(GameD3D12RootSnapshot& root, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table) {
    auto* arg = RootArg(root, index);
    if (!arg || table.ptr == 0) return;
    *arg = {};
    arg->type = GameD3D12RootArgType::DescriptorTable;
    arg->table = table;
}

void RecordConstant(GameD3D12RootSnapshot& root, UINT index, UINT value, UINT offset) {
    auto* arg = RootArg(root, index);
    if (!arg || offset >= kGameD3D12MaxRootConstants) return;
    if (arg->type != GameD3D12RootArgType::Constants) {
        *arg = {};
        arg->type = GameD3D12RootArgType::Constants;
    }
    arg->constants[offset] = value;
    arg->constantsMask |= std::uint64_t{1} << offset;
}

void RecordConstants(GameD3D12RootSnapshot& root, UINT index, UINT count, const void* data, UINT offset) {
    if (!data || offset >= kGameD3D12MaxRootConstants) return;
    const UINT safeCount = std::min<UINT>(count, static_cast<UINT>(kGameD3D12MaxRootConstants - offset));
    const auto* values = static_cast<const UINT*>(data);
    for (UINT i = 0; i < safeCount; ++i) RecordConstant(root, index, values[i], offset + i);
}

void RecordView(GameD3D12RootSnapshot& root, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address,
                GameD3D12RootArgType type) {
    auto* arg = RootArg(root, index);
    if (!arg || address == 0) return;
    *arg = {};
    arg->type = type;
    arg->address = address;
}

HRESULT STDMETHODCALLTYPE HookReset(ID3D12GraphicsCommandList* list, ID3D12CommandAllocator* allocator,
                                    ID3D12PipelineState* initialState) {
    const HRESULT result = g_reset(list, allocator, initialState);
    if (SUCCEEDED(result) && Recording()) {
        std::lock_guard lock(g_stateMutex);
        auto& state = State(list);
        state = {};
        state.pipelineState = initialState;
    }
    return result;
}

void STDMETHODCALLTYPE HookSetPipelineState(ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
    if (Recording()) { std::lock_guard lock(g_stateMutex); State(list).pipelineState = state; }
    g_setPipelineState(list, state);
}

void STDMETHODCALLTYPE HookSetDescriptorHeaps(ID3D12GraphicsCommandList* list, UINT count,
                                              ID3D12DescriptorHeap* const* heaps) {
    if (Recording()) {
        std::lock_guard lock(g_stateMutex);
        auto& state = State(list);
        state.descriptorHeapCount = std::min<UINT>(count, 2);
        state.descriptorHeaps = {};
        for (UINT i = 0; i < state.descriptorHeapCount; ++i) state.descriptorHeaps[i] = heaps ? heaps[i] : nullptr;
    }
    g_setDescriptorHeaps(list, count, heaps);
}

#define NRF_ROOT_SIG_HOOK(Name, Field, Original) \
void STDMETHODCALLTYPE Name(ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature) { \
    if (Recording()) { std::lock_guard lock(g_stateMutex); BindRoot(State(list).Field, signature); } \
    Original(list, signature); \
}
NRF_ROOT_SIG_HOOK(HookSetComputeRootSignature, compute, g_setComputeRootSignature)
NRF_ROOT_SIG_HOOK(HookSetGraphicsRootSignature, graphics, g_setGraphicsRootSignature)
#undef NRF_ROOT_SIG_HOOK

#define NRF_TABLE_HOOK(Name, Field, Original) \
void STDMETHODCALLTYPE Name(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_DESCRIPTOR_HANDLE table) { \
    if (Recording()) { std::lock_guard lock(g_stateMutex); RecordTable(State(list).Field, index, table); } \
    Original(list, index, table); \
}
NRF_TABLE_HOOK(HookSetComputeRootDescriptorTable, compute, g_setComputeRootDescriptorTable)
NRF_TABLE_HOOK(HookSetGraphicsRootDescriptorTable, graphics, g_setGraphicsRootDescriptorTable)
#undef NRF_TABLE_HOOK

#define NRF_CONSTANT_HOOK(Name, Field, Original) \
void STDMETHODCALLTYPE Name(ID3D12GraphicsCommandList* list, UINT index, UINT value, UINT offset) { \
    if (Recording()) { std::lock_guard lock(g_stateMutex); RecordConstant(State(list).Field, index, value, offset); } \
    Original(list, index, value, offset); \
}
NRF_CONSTANT_HOOK(HookSetComputeRoot32BitConstant, compute, g_setComputeRoot32BitConstant)
NRF_CONSTANT_HOOK(HookSetGraphicsRoot32BitConstant, graphics, g_setGraphicsRoot32BitConstant)
#undef NRF_CONSTANT_HOOK

#define NRF_CONSTANTS_HOOK(Name, Field, Original) \
void STDMETHODCALLTYPE Name(ID3D12GraphicsCommandList* list, UINT index, UINT count, const void* data, UINT offset) { \
    if (Recording()) { std::lock_guard lock(g_stateMutex); RecordConstants(State(list).Field, index, count, data, offset); } \
    Original(list, index, count, data, offset); \
}
NRF_CONSTANTS_HOOK(HookSetComputeRoot32BitConstants, compute, g_setComputeRoot32BitConstants)
NRF_CONSTANTS_HOOK(HookSetGraphicsRoot32BitConstants, graphics, g_setGraphicsRoot32BitConstants)
#undef NRF_CONSTANTS_HOOK

#define NRF_VIEW_HOOK(Name, Field, Type, Original) \
void STDMETHODCALLTYPE Name(ID3D12GraphicsCommandList* list, UINT index, D3D12_GPU_VIRTUAL_ADDRESS address) { \
    if (Recording()) { std::lock_guard lock(g_stateMutex); RecordView(State(list).Field, index, address, Type); } \
    Original(list, index, address); \
}
NRF_VIEW_HOOK(HookSetComputeRootConstantBufferView, compute, GameD3D12RootArgType::ConstantBufferView, g_setComputeRootConstantBufferView)
NRF_VIEW_HOOK(HookSetGraphicsRootConstantBufferView, graphics, GameD3D12RootArgType::ConstantBufferView, g_setGraphicsRootConstantBufferView)
NRF_VIEW_HOOK(HookSetComputeRootShaderResourceView, compute, GameD3D12RootArgType::ShaderResourceView, g_setComputeRootShaderResourceView)
NRF_VIEW_HOOK(HookSetGraphicsRootShaderResourceView, graphics, GameD3D12RootArgType::ShaderResourceView, g_setGraphicsRootShaderResourceView)
NRF_VIEW_HOOK(HookSetComputeRootUnorderedAccessView, compute, GameD3D12RootArgType::UnorderedAccessView, g_setComputeRootUnorderedAccessView)
NRF_VIEW_HOOK(HookSetGraphicsRootUnorderedAccessView, graphics, GameD3D12RootArgType::UnorderedAccessView, g_setGraphicsRootUnorderedAccessView)
#undef NRF_VIEW_HOOK

struct HookSpec { int index; void* hook; void** original; };

bool RestoreRoot(ID3D12GraphicsCommandList* list, const GameD3D12RootSnapshot& root,
                 SetRootSignatureFn signatureFn, SetRootDescriptorTableFn tableFn,
                 SetRoot32BitConstantFn constantFn, SetRootViewFn cbvFn,
                 SetRootViewFn srvFn, SetRootViewFn uavFn) {
    if (!root.signature) return true;
    if (!signatureFn) return false;
    signatureFn(list, root.signature);
    for (UINT index = 0; index < root.args.size(); ++index) {
        const auto& arg = root.args[index];
        switch (arg.type) {
        case GameD3D12RootArgType::None: break;
        case GameD3D12RootArgType::DescriptorTable:
            if (!tableFn) return false;
            tableFn(list, index, arg.table);
            break;
        case GameD3D12RootArgType::Constants:
            if (!constantFn) return false;
            for (UINT offset = 0; offset < kGameD3D12MaxRootConstants; ++offset)
                if ((arg.constantsMask >> offset) & 1u) constantFn(list, index, arg.constants[offset], offset);
            break;
        case GameD3D12RootArgType::ConstantBufferView:
            if (!cbvFn) return false; cbvFn(list, index, arg.address); break;
        case GameD3D12RootArgType::ShaderResourceView:
            if (!srvFn) return false; srvFn(list, index, arg.address); break;
        case GameD3D12RootArgType::UnorderedAccessView:
            if (!uavFn) return false; uavFn(list, index, arg.address); break;
        }
    }
    return true;
}

} // namespace

ScopedGameD3D12CommandStateSuppression::ScopedGameD3D12CommandStateSuppression() noexcept { ++g_suppressionDepth; }
ScopedGameD3D12CommandStateSuppression::~ScopedGameD3D12CommandStateSuppression() { --g_suppressionDepth; }

bool InstallGameD3D12CommandStateTracking(ID3D12GraphicsCommandList* commandList) noexcept {
    if (!commandList) return false;
    std::lock_guard lock(g_installMutex);
    if (g_installed) return true;
    void** vtable = *reinterpret_cast<void***>(commandList);
    if (!vtable) return false;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return false;

    HookSpec hooks[] = {
        {kReset, reinterpret_cast<void*>(&HookReset), reinterpret_cast<void**>(&g_reset)},
        {kSetPipelineState, reinterpret_cast<void*>(&HookSetPipelineState), reinterpret_cast<void**>(&g_setPipelineState)},
        {kSetDescriptorHeaps, reinterpret_cast<void*>(&HookSetDescriptorHeaps), reinterpret_cast<void**>(&g_setDescriptorHeaps)},
        {kSetComputeRootSignature, reinterpret_cast<void*>(&HookSetComputeRootSignature), reinterpret_cast<void**>(&g_setComputeRootSignature)},
        {kSetGraphicsRootSignature, reinterpret_cast<void*>(&HookSetGraphicsRootSignature), reinterpret_cast<void**>(&g_setGraphicsRootSignature)},
        {kSetComputeRootDescriptorTable, reinterpret_cast<void*>(&HookSetComputeRootDescriptorTable), reinterpret_cast<void**>(&g_setComputeRootDescriptorTable)},
        {kSetGraphicsRootDescriptorTable, reinterpret_cast<void*>(&HookSetGraphicsRootDescriptorTable), reinterpret_cast<void**>(&g_setGraphicsRootDescriptorTable)},
        {kSetComputeRoot32BitConstant, reinterpret_cast<void*>(&HookSetComputeRoot32BitConstant), reinterpret_cast<void**>(&g_setComputeRoot32BitConstant)},
        {kSetGraphicsRoot32BitConstant, reinterpret_cast<void*>(&HookSetGraphicsRoot32BitConstant), reinterpret_cast<void**>(&g_setGraphicsRoot32BitConstant)},
        {kSetComputeRoot32BitConstants, reinterpret_cast<void*>(&HookSetComputeRoot32BitConstants), reinterpret_cast<void**>(&g_setComputeRoot32BitConstants)},
        {kSetGraphicsRoot32BitConstants, reinterpret_cast<void*>(&HookSetGraphicsRoot32BitConstants), reinterpret_cast<void**>(&g_setGraphicsRoot32BitConstants)},
        {kSetComputeRootConstantBufferView, reinterpret_cast<void*>(&HookSetComputeRootConstantBufferView), reinterpret_cast<void**>(&g_setComputeRootConstantBufferView)},
        {kSetGraphicsRootConstantBufferView, reinterpret_cast<void*>(&HookSetGraphicsRootConstantBufferView), reinterpret_cast<void**>(&g_setGraphicsRootConstantBufferView)},
        {kSetComputeRootShaderResourceView, reinterpret_cast<void*>(&HookSetComputeRootShaderResourceView), reinterpret_cast<void**>(&g_setComputeRootShaderResourceView)},
        {kSetGraphicsRootShaderResourceView, reinterpret_cast<void*>(&HookSetGraphicsRootShaderResourceView), reinterpret_cast<void**>(&g_setGraphicsRootShaderResourceView)},
        {kSetComputeRootUnorderedAccessView, reinterpret_cast<void*>(&HookSetComputeRootUnorderedAccessView), reinterpret_cast<void**>(&g_setComputeRootUnorderedAccessView)},
        {kSetGraphicsRootUnorderedAccessView, reinterpret_cast<void*>(&HookSetGraphicsRootUnorderedAccessView), reinterpret_cast<void**>(&g_setGraphicsRootUnorderedAccessView)},
    };

    std::size_t created = 0;
    for (const auto& hook : hooks) {
        const MH_STATUS result = MH_CreateHook(vtable[hook.index], hook.hook, hook.original);
        if (result != MH_OK) {
            for (std::size_t i = 0; i < created; ++i) MH_RemoveHook(vtable[hooks[i].index]);
            NRF_LOG_ERROR("CommandState", "D3D12 command-state hook failed index=%d status=%s", hook.index, MH_StatusToString(result));
            return false;
        }
        ++created;
    }
    for (const auto& hook : hooks) MH_QueueEnableHook(vtable[hook.index]);
    if (MH_ApplyQueued() != MH_OK) {
        for (const auto& hook : hooks) { MH_DisableHook(vtable[hook.index]); MH_RemoveHook(vtable[hook.index]); }
        return false;
    }
    g_installed = true;
    NRF_LOG_INFO("CommandState", "D3D12 command-list state tracking installed");
    return true;
}

bool SnapshotGameD3D12CommandState(ID3D12GraphicsCommandList* commandList,
                                   GameD3D12CommandStateSnapshot& snapshot) noexcept {
    if (!commandList || !g_installed) return false;
    std::lock_guard lock(g_stateMutex);
    const auto found = g_states.find(commandList);
    if (found == g_states.end()) return false;
    snapshot = found->second;
    return snapshot.compute.signature != nullptr || snapshot.graphics.signature != nullptr;
}

bool RestoreGameD3D12CommandState(ID3D12GraphicsCommandList* commandList,
                                  const GameD3D12CommandStateSnapshot& snapshot) noexcept {
    if (!commandList || !g_installed) return false;
    ScopedGameD3D12CommandStateSuppression suppress;
    if (snapshot.descriptorHeapCount > 0) {
        if (!g_setDescriptorHeaps) return false;
        g_setDescriptorHeaps(commandList, snapshot.descriptorHeapCount, snapshot.descriptorHeaps.data());
    }
    if (!RestoreRoot(commandList, snapshot.compute, g_setComputeRootSignature,
            g_setComputeRootDescriptorTable, g_setComputeRoot32BitConstant,
            g_setComputeRootConstantBufferView, g_setComputeRootShaderResourceView,
            g_setComputeRootUnorderedAccessView)) return false;
    if (!RestoreRoot(commandList, snapshot.graphics, g_setGraphicsRootSignature,
            g_setGraphicsRootDescriptorTable, g_setGraphicsRoot32BitConstant,
            g_setGraphicsRootConstantBufferView, g_setGraphicsRootShaderResourceView,
            g_setGraphicsRootUnorderedAccessView)) return false;
    if (snapshot.pipelineState) {
        if (!g_setPipelineState) return false;
        g_setPipelineState(commandList, snapshot.pipelineState);
    }
    return true;
}

} // namespace nrfusion
