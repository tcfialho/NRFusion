#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <nvapi.h>

#include "NrKernelProfileD3D12.hpp"
#include "NrKernelAbiDiscovery.hpp"
#include "NrKernelCapture.hpp"
#include "nrfusion/Sha256.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

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

    }
    return result;
}

NvAPI_Status __cdecl DestroyFunction(ID3D12Device* device, NVDX_ObjectHandle function) {
    auto& state = Nvapi();
    const auto result = state.destroyFunction.load()(device, function);
    if (result == NVAPI_OK) {
        std::lock_guard lock(state.mutex);
        state.functions.erase(function);
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
            record.chainCount = count;
            record.chainIndex = index;
            ObserveLaunch(query, record, kernel.pParams);
        }
    }
    if (query != UINT32_MAX) StartChainTimer(commands, query);
    const auto result = state.launch.load()(commands, kernels, count);
    if (query != UINT32_MAX) EndChain(commands, query, result == NVAPI_OK);
    CaptureAfter(commands, result == NVAPI_OK);
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

bool NvapiObservationEnabled() noexcept {
    return Nvapi().enabled.load(std::memory_order_relaxed);
}

void* InterceptNvapiInterface(std::uint32_t id, void* original) noexcept {
    if (!original) return original;
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
