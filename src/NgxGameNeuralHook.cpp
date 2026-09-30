#include "NgxGameNeuralHook.hpp"
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/Logger.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "GameNeuralControl.hpp"
#include <MinHook.h>
#include <cwchar>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace nrfusion {
namespace {

using CreateFeature = int(__cdecl*)(ID3D12GraphicsCommandList*, int, NgxParameter*, void**);
using EvaluateFeature = int(__cdecl*)(ID3D12GraphicsCommandList*, const void*, NgxParameter*, void*);
using ReleaseFeature = int(__cdecl*)(void*);
CreateFeature originalCreate = nullptr;
EvaluateFeature originalEvaluate = nullptr;
ReleaseFeature originalRelease = nullptr;
thread_local bool internalNeuralCall = false;
std::mutex featureMutex;
std::mutex hookMutex;
bool hooksReady = false;

struct GameFeature {
    std::mutex mutex;
    D3D12NrExecutor executor;
    GameNeuralTiming timings;
    GameNeuralControl control;
    unsigned int createFlags = 0;
    bool rayReconstruction = false;
    bool loadAttempted = false;
    bool ready = false;
    std::uint64_t epoch = 0;
    std::uint64_t calls = 0;
    std::uint64_t applied = 0;
    std::string lastStatus;
};
std::unordered_map<const void*, std::shared_ptr<GameFeature>> features;

int __cdecl HookCreate(ID3D12GraphicsCommandList* commands, int featureId, NgxParameter* parameters, void** handle) {
    const int result = originalCreate(commands, featureId, parameters, handle);
    if (internalNeuralCall || result != 1 || !handle || !*handle || !parameters ||
        (featureId != 1 && featureId != 13)) return result;
    auto feature = std::make_shared<GameFeature>();
    parameters->Get("DLSS.Feature.Create.Flags", &feature->createFlags);
    feature->rayReconstruction = featureId == 13;
    {
        std::lock_guard lock(featureMutex);
        features[*handle] = feature;
    }
    NRF_LOG_INFO("NeuralHook", "Captured game NGX feature=%p id=%d flags=%u", *handle, featureId, feature->createFlags);
    return result;
}

int __cdecl HookEvaluate(ID3D12GraphicsCommandList* commands, const void* handle, NgxParameter* parameters, void* callback) {
    if (internalNeuralCall || !commands || !parameters) return originalEvaluate(commands, handle, parameters, callback);
    std::shared_ptr<GameFeature> feature;
    {
        std::lock_guard lock(featureMutex);
        const auto found = features.find(handle);
        if (found != features.end()) feature = found->second;
    }
    if (!feature) return originalEvaluate(commands, handle, parameters, callback);
    std::lock_guard featureLock(feature->mutex);
    auto& overlay = RuntimeOverlay::Instance();
    RuntimeConfig config{};
    RuntimeAdvancedConfig advanced{};
    overlay.GetActiveConfiguration(config, advanced);
    DlssgTransfusion::Instance().ObserveRenderedFrame();
    feature->control.Reconfigure(config, advanced);
    feature->control.Consume(feature->timings);
    ++feature->calls;
    if (!config.enabled) {
        overlay.ObserveNeuralFrame(false);
        return originalEvaluate(commands, handle, parameters, callback);
    }
    struct InternalCall {
        InternalCall() { internalNeuralCall = true; }
        ~InternalCall() { internalNeuralCall = false; }
    } internalCall;
    if (!feature->loadAttempted) {
        feature->loadAttempted = true;
        ID3D12Device* device = nullptr;
        if (SUCCEEDED(commands->GetDevice(IID_PPV_ARGS(&device))) && device) {
            feature->ready = feature->executor.Load() && feature->executor.Init(device);
            if (feature->ready && !feature->timings.Initialize(device))
                NRF_LOG_WARN("NeuralTiming", "GPU timers unavailable; Auto preserves current scale");
            device->Release();
        }
        NRF_LOG_INFO("NeuralHook", "NR initialization ready=%d status=%s", feature->ready, feature->executor.Status().c_str());
    }
    overlay.ObserveNeuralRuntime(feature->ready, feature->rayReconstruction);
    GameNeuralFrameContext context{};
    context.advanced = advanced;
    context.createFlags = feature->createFlags;
    context.epoch = ++feature->epoch;
    context.rayReconstruction = feature->rayReconstruction;
    context.workingScale = feature->control.WorkingScale();
    context.runBeforeUpscale = advanced.nr.placement == NrPlacement::Auto || advanced.nr.placement == NrPlacement::PreSr ||
                              advanced.nr.placement == NrPlacement::AcrossRr;
    const bool carryResidual = context.runBeforeUpscale && feature->rayReconstruction && advanced.nr.residualEnabled;
    const UINT timing = feature->ready && (feature->calls % 8 == 0)
        ? feature->timings.Begin(commands, feature->control.Generation(), context.workingScale) : UINT32_MAX;
    feature->timings.Mark(commands, timing, 0);
    auto result = D3D12NrFrameResult::SkippedPlacement;
    if (feature->ready && context.runBeforeUpscale) {
        context.beforeUpscale = true;
        result = ExecuteGameNeuralFrame(feature->executor, commands, parameters, context);
    }
    feature->timings.Mark(commands, timing, 1);
    const bool appliedBefore = result == D3D12NrFrameResult::Applied;
    const int driverResult = originalEvaluate(commands, handle, parameters, callback);
    feature->timings.Mark(commands, timing, 2);
    if (driverResult == 1 && feature->ready && (!context.runBeforeUpscale || carryResidual)) {
        context.beforeUpscale = false;
        result = ExecuteGameNeuralFrame(feature->executor, commands, parameters, context);
    }
    feature->timings.Mark(commands, timing, 3);
    const bool applied = driverResult == 1 && (appliedBefore || result == D3D12NrFrameResult::Applied);
    feature->timings.Finish(commands, timing, applied);
    overlay.ObserveNeuralFrame(applied);
    overlay.ObserveNeuralWork(context.workingScale, context.runBeforeUpscale,
        advanced.nr.multipassEnabled ? advanced.nr.passCount : 1, feature->control.GpuMilliseconds());
    if (applied) ++feature->applied;
    const auto& status = feature->executor.Status();
    if (status != feature->lastStatus) {
        feature->lastStatus = status;
        NRF_LOG_INFO("NeuralHook", "Game NR handle=%p result=%u applied=%llu calls=%llu before=%d scale=%.2f passes=%u style=%d gpu_ms=%.3f status=%s",
            handle, static_cast<unsigned>(result), feature->applied, feature->calls, context.runBeforeUpscale,
            context.workingScale, advanced.nr.multipassEnabled ? advanced.nr.passCount : 1,
            static_cast<int>(advanced.nr.appearance.style), feature->control.GpuMilliseconds(), status.c_str());
    }
    return driverResult;
}

int __cdecl HookRelease(void* handle) {
    std::shared_ptr<GameFeature> feature;
    {
        std::lock_guard lock(featureMutex);
        const auto found = features.find(handle);
        if (found != features.end()) {
            feature = found->second;
            features.erase(found);
        }
    }
    if (feature) {
        std::lock_guard lock(feature->mutex);
        feature->executor.Shutdown();
    }
    return originalRelease(handle);
}

} // namespace

ScopedGameNeuralHookBypass::ScopedGameNeuralHookBypass() noexcept
    : previous_(internalNeuralCall) {
    internalNeuralCall = true;
}

ScopedGameNeuralHookBypass::~ScopedGameNeuralHookBypass() {
    internalNeuralCall = previous_;
}

void TryInstallGameNeuralHooks(HMODULE module) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return;
    const wchar_t* slash = std::wcsrchr(path, L'\\');
    const wchar_t* name = slash ? slash + 1 : path;
    if (_wcsicmp(name, L"_nvngx.dll") != 0) return;
    std::lock_guard lock(hookMutex);
    if (hooksReady) return;
    void* targets[] = {
        reinterpret_cast<void*>(GetProcAddress(module, "NVSDK_NGX_D3D12_CreateFeature")),
        reinterpret_cast<void*>(GetProcAddress(module, "NVSDK_NGX_D3D12_EvaluateFeature")),
        reinterpret_cast<void*>(GetProcAddress(module, "NVSDK_NGX_D3D12_ReleaseFeature"))};
    if (!targets[0] || !targets[1] || !targets[2]) return;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return;
    void* hooks[] = {reinterpret_cast<void*>(&HookCreate), reinterpret_cast<void*>(&HookEvaluate), reinterpret_cast<void*>(&HookRelease)};
    void** originals[] = {reinterpret_cast<void**>(&originalCreate), reinterpret_cast<void**>(&originalEvaluate), reinterpret_cast<void**>(&originalRelease)};
    for (int index = 0; index < 3; ++index) {
        const auto result = MH_CreateHook(targets[index], hooks[index], originals[index]);
        if (result != MH_OK) {
            NRF_LOG_ERROR("NeuralHook", "NGX hook creation failed: %s", MH_StatusToString(result));
            for (int previous = 0; previous < index; ++previous) MH_RemoveHook(targets[previous]);
            return;
        }
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(targets[0]), &pinned)) {
        for (void* target : targets) MH_RemoveHook(target);
        return;
    }
    for (void* target : targets) MH_QueueEnableHook(target);
    hooksReady = MH_ApplyQueued() == MH_OK;
    if (!hooksReady) {
        for (void* target : targets) { MH_DisableHook(target); MH_RemoveHook(target); }
    }
    NRF_LOG_INFO("NeuralHook", "Real game NGX hooks ready=%d module=%ls", hooksReady, path);
}

} // namespace nrfusion
