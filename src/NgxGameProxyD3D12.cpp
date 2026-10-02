#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include "nrfusion/D3D12NrExecutor.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "NgxGameProxyDiagnostics.hpp"
#include "NgxGameProxyOverlay.hpp"
#include "NgxGameNeuralHook.hpp"
#include "NrKernelResourceObservation.hpp"
#include "NrKernelProfileD3D12.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
extern "C" void NRFusion_EnsureRuntime();
extern "C" void NRFusion_ShutdownRuntime();

namespace {

constexpr int kNgxSuccess = 1;
constexpr int kFeatureSuperSampling = 1;

using Param = nrfusion::NgxParameter;
using InitFn = int(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*,
                             const void*, unsigned int);
using AllocFn = int(__cdecl*)(Param**);
using CreateFn = int(__cdecl*)(ID3D12GraphicsCommandList*, int, Param*, void**);
using EvalFn = int(__cdecl*)(ID3D12GraphicsCommandList*, const void*, Param*, void*);
using ReleaseFn = int(__cdecl*)(void*);
using ShutdownFn = int(__cdecl*)();
struct DriverApi {
    HMODULE module = nullptr;
    InitFn init = nullptr;
    AllocFn allocate = nullptr;
    AllocFn capabilities = nullptr;
    CreateFn create = nullptr;
    EvalFn evaluate = nullptr;
    ReleaseFn release = nullptr;
    ShutdownFn shutdown = nullptr;

    bool Ready() const noexcept {
        return module && init && (allocate || capabilities) &&
               create && evaluate && release && shutdown;
    }
};

template <typename T>
T Symbol(HMODULE module, const char* name) {
    return reinterpret_cast<T>(
        reinterpret_cast<void*>(GetProcAddress(module, name)));
}

bool IsNgxDriver(HMODULE module) {
    return module &&
        GetProcAddress(module, "NVSDK_NGX_D3D12_GetCapabilityParameters") &&
        GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature");
}
HMODULE LoadFromDriverStore() {
    wchar_t systemDir[MAX_PATH]{};
    if (GetSystemDirectoryW(systemDir, MAX_PATH) == 0) return nullptr;
    std::wstring pattern = std::wstring(systemDir) + L"\\DriverStore\\FileRepository\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return nullptr;
    HMODULE result = nullptr;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || data.cFileName[0] == L'.') continue;
        const std::wstring path = std::wstring(systemDir) + L"\\DriverStore\\FileRepository\\" + data.cFileName + L"\\nvngx.dll";
        HMODULE module = LoadLibraryW(path.c_str());
        if (IsNgxDriver(module)) { result = module; break; }
        if (module) FreeLibrary(module);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return result;
}
DriverApi ResolveDriver() {
    static constexpr std::array<const wchar_t*, 3> candidates{L"nvngx.dll", L"_nvngx.dll", L"nvngx_dlss.dll"};
    HMODULE module = nullptr;
    for (const wchar_t* candidate : candidates) {
        module = GetModuleHandleW(candidate);
        if (!module) module = LoadLibraryW(candidate);
        if (IsNgxDriver(module)) break;
        if (module) FreeLibrary(module);
        module = nullptr;
    }
    if (!module) module = LoadFromDriverStore();
    DriverApi api{};
    api.module = module;
    if (!module) return api;
    api.init = Symbol<InitFn>(module, "NVSDK_NGX_D3D12_Init");
    api.allocate = Symbol<AllocFn>(module, "NVSDK_NGX_D3D12_AllocateParameters");
    api.capabilities = Symbol<AllocFn>(module, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    api.create = Symbol<CreateFn>(module, "NVSDK_NGX_D3D12_CreateFeature");
    api.evaluate = Symbol<EvalFn>(module, "NVSDK_NGX_D3D12_EvaluateFeature");
    api.release = Symbol<ReleaseFn>(module, "NVSDK_NGX_D3D12_ReleaseFeature");
    api.shutdown = Symbol<ShutdownFn>(module, "NVSDK_NGX_D3D12_Shutdown");
    return api;
}
DriverApi& Driver() {
    static DriverApi api = ResolveDriver();
    return api;
}

struct ProxyFeature {
    ID3D12Device* diagnosticDevice = nullptr;
    void* driverFeature = nullptr;
    nrfusion::D3D12NrExecutor nr;
    nrfusion::NgxGameProxyOverlay overlay;
    std::uint64_t epoch = 0;
    unsigned int createFlags = 0;
    bool nrReady = false;
    ID3D12Resource* cachedColor = nullptr;
    ID3D12Resource* cachedDepth = nullptr;
    ID3D12Resource* cachedMotion = nullptr;
    D3D12_RESOURCE_DESC cachedColorDesc{};
    D3D12_RESOURCE_DESC cachedDepthDesc{};
    D3D12_RESOURCE_DESC cachedMotionDesc{};
};

inline unsigned int GetUInt(Param* params, const char* name, unsigned int fallback = 0) {
    if (!params) return fallback;
    unsigned int value = fallback;
    if (params->Get(name, &value) == kNgxSuccess) return value;
    int signedValue = static_cast<int>(fallback);
    return params->Get(name, &signedValue) == kNgxSuccess
        ? static_cast<unsigned int>(signedValue) : fallback;
}

inline float GetFloat(Param* params, const char* name, float fallback) {
    float value = fallback;
    return params && params->Get(name, &value) == kNgxSuccess ? value : fallback;
}

inline ID3D12Resource* GetResource(Param* params, const char* name) {
    ID3D12Resource* value = nullptr;
    return params && params->Get(name, &value) == kNgxSuccess ? value : nullptr;
}

bool RunNeuralPass(ProxyFeature& feature, ID3D12GraphicsCommandList* commands,
                   Param* params) {
    if (!feature.nrReady || !commands || !params) return false;
    if (!nrfusion::RuntimeOverlay::Instance().IsActiveEnabled()) return false;

    ID3D12Resource* color = GetResource(params, "Color");
    ID3D12Resource* depth = GetResource(params, "Depth");
    ID3D12Resource* motion = GetResource(params, "MotionVectors");
    ID3D12Resource* output = GetResource(params, "Output");
    if (!color || !depth || !motion || !output) return false;

    if (feature.cachedColor != color) {
        feature.cachedColor = color;
        feature.cachedColorDesc = color->GetDesc();
    }
    const auto& colorDesc = feature.cachedColorDesc;
    if (feature.cachedDepth != depth) {
        feature.cachedDepth = depth;
        feature.cachedDepthDesc = depth->GetDesc();
    }
    const auto& depthDesc = feature.cachedDepthDesc;
    if (feature.cachedMotion != motion) {
        feature.cachedMotion = motion;
        feature.cachedMotionDesc = motion->GetDesc();
    }
    const auto& motionDesc = feature.cachedMotionDesc;
    if (colorDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        depthDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        motionDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return false;

    nrfusion::D3D12NrFrameResources resources{};
    resources.color = color;
    resources.depth = depth;
    resources.motion = motion;
    resources.output = output;
    nrfusion::D3D12NrFrameRequest request{};
    request.plan.colorSurface = {
        static_cast<std::uint32_t>(colorDesc.Width), colorDesc.Height};
    request.plan.depthSurface = {
        static_cast<std::uint32_t>(depthDesc.Width), depthDesc.Height};
    request.plan.motionSurface = {
        static_cast<std::uint32_t>(motionDesc.Width), motionDesc.Height};
    request.plan.activeColor = {
        0, 0, request.plan.colorSurface.width, request.plan.colorSurface.height};
    request.plan.depth = {
        0, 0, request.plan.depthSurface.width, request.plan.depthSurface.height};
    request.plan.motion = {
        0, 0, request.plan.motionSurface.width, request.plan.motionSurface.height};
    request.plan.execution.workingScale = 1.0f;
    request.plan.execution.proxyBackend = true;
    request.plan.beforeUpscale = true;
    request.composition.runBeforeUpscale = true;
    request.composition.colourIsLinearHdr = false;
    request.submissionEpoch = ++feature.epoch;
    request.reset = GetUInt(params, "Reset") != 0;
    request.depthInverted = (feature.createFlags & (1u << 3)) != 0;
    request.motionScaleX = GetFloat(params, "MV.Scale.X", 1.0f);
    request.motionScaleY = GetFloat(params, "MV.Scale.Y", 1.0f);
    request.colorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    request.depthState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    request.motionState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    request.outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    nrfusion::ngxproxy::BeginDiagnosticPass(commands);
    request.recordGpuStage = nrfusion::ngxproxy::DiagnosticGpuStageRecorder();
    const auto result = feature.nr.ExecuteFrame(commands, resources, request);
    nrfusion::ngxproxy::EndDiagnosticPass(
        commands, result == nrfusion::D3D12NrFrameResult::Applied);
    return result == nrfusion::D3D12NrFrameResult::Applied ||
           result == nrfusion::D3D12NrFrameResult::PendingFeature;
}

} // namespace
extern "C" __declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_Init(
    unsigned long long appId, const wchar_t* appPath, ID3D12Device* device,
    const void* info, unsigned int version) {
    nrfusion::RuntimeOverlay::Instance().SetInFrameRendering(true);
    NRFusion_EnsureRuntime();
    nrfusion::kernelprofile::StartResourceObservation(device);
    auto& driver = Driver();
    if (!driver.Ready()) return 0;
    return driver.init(appId, appPath, device, info, version);
}
extern "C" __declspec(dllexport) int __cdecl
NVSDK_NGX_D3D12_AllocateParameters(Param** params) {
    auto& driver = Driver();
    return driver.allocate ? driver.allocate(params) : 0;
}
extern "C" __declspec(dllexport) int __cdecl
NVSDK_NGX_D3D12_GetCapabilityParameters(Param** params) {
    auto& driver = Driver();
    return driver.capabilities ? driver.capabilities(params) : 0;
}
extern "C" __declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_CreateFeature(
    ID3D12GraphicsCommandList* commands, int featureId, Param* params,
    void** outputFeature) {
    if (!commands || !params || !outputFeature) return 0;
    nrfusion::kernelprofile::ObserveCommandBarriers(commands);
    auto& driver = Driver();
    if (!driver.Ready()) return 0;

    void* driverFeature = nullptr;
    nrfusion::ScopedGameNeuralHookBypass neuralBypass;
    const int result = driver.create(
        commands, featureId, params, &driverFeature);
    if (result != kNgxSuccess || !driverFeature) return result;
    auto proxy = std::make_unique<ProxyFeature>();
    proxy->driverFeature = driverFeature;
    proxy->createFlags = GetUInt(params, "DLSS.Feature.Create.Flags");
    if (featureId == kFeatureSuperSampling) {
        ID3D12Device* device = nullptr;
        if (SUCCEEDED(commands->GetDevice(IID_PPV_ARGS(&device))) && device) {
            proxy->diagnosticDevice = device;
            proxy->nrReady = proxy->nr.Load() && proxy->nr.Init(device);
            device->Release();
        }
    }
    *outputFeature = proxy.release();
    return result;
}
extern "C" __declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_EvaluateFeature(
    ID3D12GraphicsCommandList* commands, const void* opaqueFeature,
    Param* params, void* callback) {
    if (!opaqueFeature) return 0;
    auto* feature = const_cast<ProxyFeature*>(
        static_cast<const ProxyFeature*>(opaqueFeature));
    RunNeuralPass(*feature, commands, params);
    auto& driver = Driver();
    if (!driver.evaluate) return 0;
    const int result = driver.evaluate(commands, feature->driverFeature, params, callback);
    if (result == kNgxSuccess && nrfusion::RuntimeOverlay::Instance().IsMenuOpen())
        feature->overlay.Draw(commands, GetResource(params, "Output"));
    return result;
}
extern "C" __declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_ReleaseFeature(void* opaqueFeature) {
    if (!opaqueFeature) return 0;
    std::unique_ptr<ProxyFeature> feature(static_cast<ProxyFeature*>(opaqueFeature));
    feature->nr.Shutdown();
    nrfusion::kernelprofile::ResetObservedResources(feature->diagnosticDevice);
    auto& driver = Driver();
    return driver.release ? driver.release(feature->driverFeature) : 0;
}
extern "C" __declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_Shutdown() {
    auto& driver = Driver();
    const int result = driver.shutdown ? driver.shutdown() : 0;
    nrfusion::RuntimeOverlay::Instance().SetInFrameRendering(false);
    NRFusion_ShutdownRuntime();
    return result;
}
