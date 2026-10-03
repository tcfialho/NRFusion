#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <nvapi.h>

#include "NrKernelProfileD3D12.hpp"
#include "NrKernelAbiDiscovery.hpp"
#include "NrKernelCapture.hpp"
#include "NrKernelChainCapture.hpp"
#include "NrSwinKernelCapture.hpp"
#include "NrKernelReplacement.hpp"
#include "nrfusion/Sha256.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nrfusion::kernelprofile {
namespace {

struct NvapiState {
    std::mutex mutex;
    std::atomic<bool> enabled{false};
    std::atomic<decltype(&NvAPI_D3D12_CreateCuModule)> createModule{nullptr};
    std::atomic<decltype(&NvAPI_D3D12_CreateCuFunction)> createFunction{nullptr};
    std::atomic<decltype(&NvAPI_D3D12_DestroyCuModule)> destroyModule{nullptr};
    std::atomic<decltype(&NvAPI_D3D12_DestroyCuFunction)> destroyFunction{nullptr};
    std::atomic<decltype(&NvAPI_D3D12_LaunchCuKernelChain)> launch{nullptr};
    std::unordered_map<NVDX_ObjectHandle, FunctionIdentity> functions;
    struct ModuleIdentity {
        std::uint64_t generation = 0;
        std::array<char, 65> hash{};
        ModuleImage image;
    };
    std::unordered_map<NVDX_ObjectHandle, ModuleIdentity> modules;
    std::uint64_t generation = 0;
};

NvapiState& Nvapi() {
    static NvapiState state;
    return state;
}

NvAPI_Status __cdecl CreateModule(ID3D12Device* device, const void* image, NvU32 bytes,
                                  NVDX_ObjectHandle* module) {
    auto& state = Nvapi();
    const auto result = state.createModule.load()(device, image, bytes, module);
    if (result == NVAPI_OK && module && state.enabled.load(std::memory_order_relaxed)) {
        NvapiState::ModuleIdentity identity{};
        identity.image = CopyAbiModule(image, bytes);
        if (image && bytes) {
            const auto hash = Sha256Hex({static_cast<const std::uint8_t*>(image), bytes});
            std::memcpy(identity.hash.data(), hash.data(), hash.size());
        }
        std::lock_guard lock(state.mutex);
        identity.generation = ++state.generation;
        state.modules[*module] = identity;
    }
    return result;
}

NvAPI_Status __cdecl CreateFunction(ID3D12Device* device, NVDX_ObjectHandle module,
                                    const char* name, NVDX_ObjectHandle* function) {
    auto& state = Nvapi();
    const auto result = state.createFunction.load()(device, module, name, function);
    if (result == NVAPI_OK && function && name && state.enabled.load(std::memory_order_relaxed)) {
        FunctionIdentity identity{};
        identity.function = reinterpret_cast<std::uintptr_t>(*function);
        identity.module = reinterpret_cast<std::uintptr_t>(module);
        identity.device = reinterpret_cast<std::uintptr_t>(device);
        const auto length = strnlen_s(name, identity.name.size());
        identity.truncated = length == identity.name.size();
        std::memcpy(identity.name.data(), name, identity.truncated ? length - 1 : length);
        std::unique_lock lock(state.mutex);
        const auto found = state.modules.find(module);
        if (found != state.modules.end()) {
            identity.generation = found->second.generation;
            identity.moduleHash = found->second.hash;
            SelectAbiFunction(identity, found->second.image);
        } else identity.generation = ++state.generation;
        state.functions[*function] = identity;
        lock.unlock();
        using Query = void*(__cdecl*)(unsigned);
        const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"nvapi64.dll"), "nvapi_QueryInterface"));
        if (query) { query(0x41c65285); query(0xdf295ea6); }
        PrepareReplacement(device, identity, state.createModule.load(), state.createFunction.load(),
                           state.destroyModule.load());
    }
    return result;
}

NvAPI_Status __cdecl DestroyFunction(ID3D12Device* device, NVDX_ObjectHandle function) {
    auto& state = Nvapi();
    const auto result = state.destroyFunction.load()(device, function);
    if (result == NVAPI_OK) {
        std::lock_guard lock(state.mutex);
        state.functions.erase(function);
        ForgetReplacement(device, function, state.destroyFunction.load(), state.destroyModule.load());
    }
    return result;
}

NvAPI_Status __cdecl DestroyModule(ID3D12Device* device, NVDX_ObjectHandle module) {
    auto& state = Nvapi();
    const auto result = state.destroyModule.load()(device, module);
    if (result == NVAPI_OK) {
        std::lock_guard lock(state.mutex);
        state.modules.erase(module);
        for (auto entry = state.functions.begin(); entry != state.functions.end();) {
            if (entry->second.module == reinterpret_cast<std::uintptr_t>(module)) {
                ForgetReplacement(device, entry->first, state.destroyFunction.load(), state.destroyModule.load());
                entry = state.functions.erase(entry);
            }
            else ++entry;
        }
    }
    return result;
}

NvAPI_Status __cdecl Launch(ID3D12GraphicsCommandList* commands,
                            const NVAPI_CU_KERNEL_LAUNCH_PARAMS* kernels, NvU32 count) {
    auto& state = Nvapi();
    NVAPI_CU_KERNEL_LAUNCH_PARAMS selected{};
    bool custom = false;
    if (count == 1 && kernels != nullptr && IsReplacementActive()) {
        custom = SelectReplacement(kernels[0], selected);
    }
    const bool enabled = state.enabled.load(std::memory_order_relaxed);
    const unsigned query = enabled ? BeginChain(commands, count) : UINT32_MAX;
    if (query != UINT32_MAX && kernels) {
        std::lock_guard lock(state.mutex);
        for (NvU32 index = 0; index < count; ++index) {
            const auto& kernel = kernels[index];
            LaunchRecord record{};
            const auto found = state.functions.find(kernel.hFunction);
            if (found != state.functions.end()) record.identity = found->second;
            else record.identity.function = reinterpret_cast<std::uintptr_t>(kernel.hFunction);
            record.commands = reinterpret_cast<std::uintptr_t>(commands);
            record.grid = {kernel.gridDim.x, kernel.gridDim.y, kernel.gridDim.z};
            record.block = {kernel.blockDim.x, kernel.blockDim.y, kernel.blockDim.z};
            record.sharedBytes = kernel.dynSharedMemBytes;
            record.parameterBytes = kernel.paramSize;
            record.query = query;
            record.custom = custom;
            record.chainCount = count;
            record.chainIndex = index;
            ObserveLaunch(query, record, kernel.pParams);
        }
    }
    if (query != UINT32_MAX) StartChainTimer(commands, query);
    const auto launchFunc = state.launch.load(std::memory_order_relaxed);
    const auto result = launchFunc(commands, custom ? &selected : kernels, count);
    if (query != UINT32_MAX) {
        EndChain(commands, query, result == NVAPI_OK);
        CaptureChainAfter(commands, result == NVAPI_OK);
    }
    if (IsKernelCaptureActive()) CaptureAfter(commands, result == NVAPI_OK);
    if (IsSwinCaptureActive()) CaptureSwinAfter(commands, result == NVAPI_OK);
    return result;
}

template <typename Function>
void* Wrap(std::atomic<Function>& target, void* original, Function replacement) {
    const auto function = reinterpret_cast<Function>(original);
    Function expected = nullptr;
    if (!target.compare_exchange_strong(expected, function) && expected != function) return original;
    return reinterpret_cast<void*>(replacement);
}

} // namespace

void EnableNvapiObservation(bool enabled) noexcept {
    if (enabled) ConfigureAbiDiscovery();
    Nvapi().enabled.store(enabled, std::memory_order_relaxed);
}

void RetireNvapiDevice(ID3D12Device* device) {
    auto& state = Nvapi();
    if (!state.enabled.load()) return;
    DeactivateReplacements(device);
    std::lock_guard lock(state.mutex);
    for (auto& [function, identity] : state.functions) {
        if (identity.device != reinterpret_cast<std::uintptr_t>(device)) continue;
        identity.generation = 0;
    }
}

void RefreshNvapiDevice(ID3D12Device* device) {
    auto& state = Nvapi();
    if (!state.enabled.load()) return;
    std::vector<FunctionIdentity> identities;
    {
        std::lock_guard lock(state.mutex);
        const auto generation = ++state.generation;
        for (auto& [function, identity] : state.functions) {
            if (identity.device != reinterpret_cast<std::uintptr_t>(device)) continue;
            identity.generation = generation;
            identities.push_back(identity);
        }
    }
    for (const auto& identity : identities)
        PrepareReplacement(device, identity, state.createModule.load(), state.createFunction.load(),
                           state.destroyModule.load());
}

bool NvapiObservationEnabled() noexcept {
    return Nvapi().enabled.load(std::memory_order_relaxed);
}

bool IsAdaGpu() noexcept {
    static int s_isAda = -1;
    if (s_isAda != -1) return s_isAda == 1;

    const auto driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!driver) {
        s_isAda = 0;
        return false;
    }
    using Init = int(__stdcall*)(unsigned);
    using Count = int(__stdcall*)(int*);
    using Attribute = int(__stdcall*)(int*, int, int);
    const auto init = reinterpret_cast<Init>(GetProcAddress(driver, "cuInit"));
    const auto count = reinterpret_cast<Count>(GetProcAddress(driver, "cuDeviceGetCount"));
    const auto attribute = reinterpret_cast<Attribute>(GetProcAddress(driver, "cuDeviceGetAttribute"));

    int devices = 0;
    bool foundAda = false;
    if (init && count && attribute && init(0) == 0 && count(&devices) == 0) {
        for (int i = 0; i < devices; ++i) {
            int major = 0, minor = 0;
            // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75
            // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76
            if (attribute(&major, 75, i) == 0 && attribute(&minor, 76, i) == 0) {
                if (major == 8 && minor == 9) {
                    foundAda = true;
                    break;
                }
            }
        }
    }
    FreeLibrary(driver);
    s_isAda = foundAda ? 1 : 0;
    return foundAda;
}

bool IsDlssgCaller(const void* callerAddress) noexcept {
    if (!callerAddress) return false;
    HMODULE callerModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(callerAddress),
            &callerModule) || !callerModule) {
        return false;
    }
    wchar_t modulePath[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(callerModule, modulePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;
    const wchar_t* fileName = wcsrchr(modulePath, L'\\');
    fileName = fileName ? fileName + 1 : modulePath;
    if (_wcsicmp(fileName, L"sl.dlss_g.dll") == 0) return true;
    _wcslwr_s(modulePath, len + 1);
    return (wcsstr(modulePath, L"sl_dlss_g") != nullptr || wcsstr(modulePath, L"sl.dlss_g") != nullptr);
}

void* InterceptNvapiInterface(std::uint32_t id, void* original, const void* callerAddress) noexcept {
    if (!original) return original;
    if (id == 0xf3148c42) {
        wchar_t pacerBuf[32]{};
        const bool pacerRequested = (GetEnvironmentVariableW(L"NRFUSION_MFG_PACER", pacerBuf, 32) > 0 &&
            (_wcsicmp(pacerBuf, L"CpuPacer") == 0 || wcscmp(pacerBuf, L"1") == 0));
        if (!pacerRequested) {
            return original;
        }
        // Scoped CPU Pacer qualification (fail-closed):
        // 1. Caller module MUST be verified as sl.dlss_g.dll
        // 2. Hardware MUST be verified as Ada Lovelace SM89
        if (!IsDlssgCaller(callerAddress) || !IsAdaGpu()) {
            return original;
        }
        return nullptr;
    }
    auto& state = Nvapi();
    switch (id) {
    case 0xad1a677d: return Wrap(state.createModule, original, &CreateModule);
    case 0xe2436e22: return Wrap(state.createFunction, original, &CreateFunction);
    case 0x41c65285: return Wrap(state.destroyModule, original, &DestroyModule);
    case 0xdf295ea6: return Wrap(state.destroyFunction, original, &DestroyFunction);
    case 0x24973538: return Wrap(state.launch, original, &Launch);
    default: return original;
    }
}

} // namespace nrfusion::kernelprofile
