#include "GameNeuralTiming.hpp"
#include "nrfusion/Logger.hpp"
#include <windows.h>
#include <mutex>
#include <unordered_map>

namespace nrfusion {
namespace {
using ExecuteCommandLists = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
std::mutex queueMutex;
std::unordered_map<void**, ExecuteCommandLists> queueOriginals;

void STDMETHODCALLTYPE HookExecute(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* commands) {
    thread_local void** cachedVtable = nullptr;
    thread_local ExecuteCommandLists cachedOriginal = nullptr;
    void** vtable = *reinterpret_cast<void***>(queue);
    if (vtable != cachedVtable) {
        std::lock_guard lock(queueMutex);
        const auto found = queueOriginals.find(vtable);
        if (found == queueOriginals.end()) return;
        cachedVtable = vtable;
        cachedOriginal = found->second;
    }
    cachedOriginal(queue, count, commands);
    if (HasPendingNeuralTimings()) SubmitNeuralTimings(queue, count, commands);
}
} // namespace

void RegisterNeuralCommandQueue(ID3D12CommandQueue* queue) {
    if (!queue) return;
    void** vtable = *reinterpret_cast<void***>(queue);
    std::lock_guard lock(queueMutex);
    if (queueOriginals.contains(vtable)) return;
    constexpr UINT executeIndex = 10;
    DWORD protection = 0;
    if (!VirtualProtect(&vtable[executeIndex], sizeof(void*), PAGE_READWRITE, &protection)) return;
    queueOriginals[vtable] = reinterpret_cast<ExecuteCommandLists>(vtable[executeIndex]);
    InterlockedExchangePointer(reinterpret_cast<PVOID*>(&vtable[executeIndex]), reinterpret_cast<void*>(&HookExecute));
    DWORD ignored = 0;
    VirtualProtect(&vtable[executeIndex], sizeof(void*), protection, &ignored);
    NRF_LOG_INFO("NeuralTiming", "Tracking real command queue submissions vtable=%p", vtable);
}
} // namespace nrfusion
