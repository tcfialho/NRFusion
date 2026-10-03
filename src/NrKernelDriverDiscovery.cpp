#include "nrfusion/NrKernelProfile.hpp"

#include <array>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <MinHook.h>
#include "NrKernelProfileD3D12.hpp"
#endif

namespace nrfusion {
namespace {

struct InterfaceState {
    std::mutex mutex;
    std::array<NrDriverInterfaceObservation, 1024> records{};
    std::size_t count = 0;
#if defined(_WIN32)
    using QueryInterface = void*(__cdecl*)(unsigned);
    QueryInterface original = nullptr;
    void* target = nullptr;
    HMODULE module = nullptr;
#endif
    bool installed = false;
};

InterfaceState& Interfaces() {
    static InterfaceState state;
    return state;
}

#if defined(_WIN32)
void* __cdecl ObserveInterface(unsigned id) {
    auto& state = Interfaces();
    void* function = state.original(id);
    std::lock_guard lock(state.mutex);
    for (std::size_t index = 0; index < state.count; ++index) {
        auto& record = state.records[index];
        if (record.interfaceId == id && record.functionId == reinterpret_cast<std::uintptr_t>(function)) {
            ++record.observations;
            return kernelprofile::InterceptNvapiInterface(id, function);
        }
    }
    if (state.count < state.records.size()) {
        state.records[state.count++] = {id, reinterpret_cast<std::uintptr_t>(function), 1};
    }
    return kernelprofile::InterceptNvapiInterface(id, function);
}
#endif

} // namespace

bool NrKernelProfiler::StartDriverDiscovery() {
#if defined(_WIN32)
    wchar_t enabled[2]{};
    const bool discoveryRequested = (GetEnvironmentVariableW(L"NRFUSION_KERNEL_DISCOVERY", enabled, 2) == 1 && enabled[0] == L'1');
    wchar_t pacerBuf[32]{};
    const bool pacerConfigured = (GetEnvironmentVariableW(L"NRFUSION_MFG_PACER", pacerBuf, 32) > 0);
    if (!discoveryRequested && !pacerConfigured)
        return false;
    auto& state = Interfaces();
    std::unique_lock lock(state.mutex);
    if (state.installed) return true;
    if (state.target) {
        if (MH_EnableHook(state.target) != MH_OK) return false;
        state.installed = true;
        lock.unlock();
        kernelprofile::EnableNvapiObservation(true);
        return true;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE module = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return false;
    void* target = reinterpret_cast<void*>(GetProcAddress(module, "nvapi_QueryInterface"));
    if (!target || MH_CreateHook(target, reinterpret_cast<void*>(&ObserveInterface),
                                 reinterpret_cast<void**>(&state.original)) != MH_OK) {
        FreeLibrary(module);
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        FreeLibrary(module);
        return false;
    }
    state.target = target;
    state.module = module;
    state.installed = true;
    lock.unlock();
    kernelprofile::EnableNvapiObservation(true);
    return true;
#else
    return false;
#endif
}

void NrKernelProfiler::StopDriverDiscovery() {
#if defined(_WIN32)
    auto& state = Interfaces();
    kernelprofile::EnableNvapiObservation(false);
    void* target = nullptr;
    {
        std::lock_guard lock(state.mutex);
        if (!state.installed) return;
        target = state.target;
    }
    if (MH_DisableHook(target) != MH_OK) return;
    std::lock_guard lock(state.mutex);
    state.installed = false;
    // The trampoline remains valid for a query already executing during shutdown.
#endif
}

std::vector<NrDriverInterfaceObservation> NrKernelProfiler::DriverInterfaces() const {
    auto& state = Interfaces();
    std::lock_guard lock(state.mutex);
    return {state.records.begin(), state.records.begin() + state.count};
}

} // namespace nrfusion
