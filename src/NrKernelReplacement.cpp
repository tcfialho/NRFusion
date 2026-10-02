#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelReplacement.hpp"
#include "NrKernelArchitecture.hpp"
#include "nrfusion/Sha256.hpp"
#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nrfusion::kernelprofile {
namespace {
struct Replacement {
    ID3D12Device* device = nullptr;
    NVDX_ObjectHandle module = nullptr, function = nullptr;
    bool eligible = false;
};
std::atomic<bool> registryActive{false};
std::mutex registryMutex;
std::unordered_map<NVDX_ObjectHandle, Replacement> registry;


bool KnownRuntime() {
    wchar_t path[32768]{};
    const auto runtime = GetModuleHandleW(L"nvngx_dlssnr.dll");
    if (!runtime || !GetModuleFileNameW(runtime, path, 32768)) return false;
    const auto hash = Sha256File(std::filesystem::path(path));
    return hash && *hash == "e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a";
}
}

void PrepareReplacement(ID3D12Device* device, const FunctionIdentity& identity,
    decltype(&NvAPI_D3D12_CreateCuModule) createModule,
    decltype(&NvAPI_D3D12_CreateCuFunction) createFunction,
    decltype(&NvAPI_D3D12_DestroyCuModule) destroyModule) {
    wchar_t mode[16]{}, path[32768]{};
    if (!GetEnvironmentVariableW(L"NRFUSION_KERNEL_REPLACEMENT_MODE", mode, 16) ||
        std::wcscmp(mode, L"Custom") || identity.truncated ||
        std::strcmp(identity.name.data(), "cc_vit_1d_ffn_expand_chained_fp8") ||
        std::strcmp(identity.moduleHash.data(), "bdc0cafe89442d2fa64ab168905e5ebcfe4bb7592604d0b4b2fca2db063b7a2b")) return;
    const auto length = GetEnvironmentVariableW(L"NRFUSION_KERNEL_REPLACEMENT_CUBIN", path, 32768);
    if (!length || length >= 32768 || !createModule || !createFunction || !destroyModule) return;
    try {
        if (!KnownRuntime() || !SupportsSm89Device(device)) return;
        const auto hash = Sha256File(std::filesystem::path(path));
        if (!hash || *hash != "35fdfab506841b69a49e43487471d4ba00a7a4b6a8d44cc7c95dd6ee15284c73") return;
        std::ifstream source(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        const auto size = source.tellg();
        if (size <= 0 || size > 16 * 1024 * 1024) return;
        std::vector<std::uint8_t> image(static_cast<std::size_t>(size));
        source.seekg(0);
        if (!source.read(reinterpret_cast<char*>(image.data()), size) || Sha256Hex(image) != *hash) return;
        {
            std::lock_guard lock(registryMutex);
            const auto found = registry.find(reinterpret_cast<NVDX_ObjectHandle>(identity.function));
            if (found != registry.end() && found->second.device == device) {
                found->second.eligible = true;
                return;
            }
        }
        Replacement replacement{device};
        replacement.eligible = true;
        if (createModule(device, image.data(), static_cast<NvU32>(image.size()), &replacement.module) != NVAPI_OK) return;
        if (createFunction(device, replacement.module, "nrfusion_ffn_reference", &replacement.function) != NVAPI_OK) {
            destroyModule(device, replacement.module);
            return;
        }
        std::lock_guard lock(registryMutex);
        registry.emplace(reinterpret_cast<NVDX_ObjectHandle>(identity.function), replacement);
        registryActive.store(true, std::memory_order_release);
    } catch (...) {
        return;
    }
}

void DeactivateReplacements(ID3D12Device* device) {
    std::lock_guard lock(registryMutex);
    for (auto& [function, replacement] : registry)
        if (replacement.device == device) replacement.eligible = false;
}

void ForgetReplacement(ID3D12Device* device, NVDX_ObjectHandle function,
    decltype(&NvAPI_D3D12_DestroyCuFunction) destroyFunction,
    decltype(&NvAPI_D3D12_DestroyCuModule) destroyModule) {
    Replacement replacement;
    {
        std::lock_guard lock(registryMutex);
        const auto found = registry.find(function);
        if (found == registry.end() || found->second.device != device) return;
        replacement = found->second;
        registry.erase(found);
    }
    if (destroyFunction) destroyFunction(device, replacement.function);
    if (destroyModule) destroyModule(device, replacement.module);
}

bool IsReplacementActive() noexcept {
    return registryActive.load(std::memory_order_relaxed);
}

bool SelectReplacement(const NVAPI_CU_KERNEL_LAUNCH_PARAMS& stock,
    NVAPI_CU_KERNEL_LAUNCH_PARAMS& selected) noexcept {
    if (!registryActive.load(std::memory_order_acquire)) return false;
    if (stock.gridDim.x != 96 || stock.gridDim.y != 1 || stock.gridDim.z != 1 ||
        stock.blockDim.x != 32 || stock.blockDim.y != 4 || stock.blockDim.z != 1 ||
        stock.dynSharedMemBytes || stock.paramSize != 72 || !stock.pParams) return false;
    std::lock_guard lock(registryMutex);
    const auto found = registry.find(stock.hFunction);
    if (found == registry.end() || !found->second.eligible) return false;
    std::uint64_t arguments[9]{};
    std::memcpy(arguments, stock.pParams, sizeof(arguments));
    if (arguments[1] || arguments[4] || arguments[5] ||
        arguments[8] != (std::uint64_t{24} << 32 | 12) ||
        !arguments[0] || !arguments[2] || !arguments[3] || !arguments[6] || !arguments[7]) return false;
    selected = stock;
    selected.hFunction = found->second.function;
    return true;
}
}
