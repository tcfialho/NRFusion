#include "StreamlineDlssgOptions.hpp"
#include "StreamlineReflexTracker.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/Logger.hpp"
#include "PeMemoryUtils.hpp"
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace nrfusion::streamline {
namespace {
using SetOptions = sl::Result(*)(const sl::ViewportHandle&, const sl::DLSSGOptions&);
using GetState = sl::Result(*)(const sl::ViewportHandle&, sl::DLSSGState&, const sl::DLSSGOptions*);
using SetReflexOptions = sl::Result(*)(const sl::ReflexOptions&);
using ReflexSleep = sl::Result(*)(const sl::FrameToken&);
using SetPclMarker = sl::Result(*)(sl::PCLMarker, const sl::FrameToken&);
using GetFeatureFunction = uint32_t(*)(uint32_t, const char*, void**);

std::atomic<SetOptions> originalSetOptions{nullptr};
std::atomic<GetState> originalGetState{nullptr};
std::atomic<SetReflexOptions> originalReflexSetOptions{nullptr};
std::atomic<ReflexSleep> originalReflexSleep{nullptr};
std::atomic<SetPclMarker> originalPclSetMarker{nullptr};
std::atomic<GetFeatureFunction> featureDispatcher{nullptr};

std::atomic<bool> updatePending{false};
std::atomic<bool> dynamicSupported{false};
std::atomic<uint32_t> optionsVersion{0};
std::atomic<uint32_t> maximumFrames{0};
std::atomic<uint32_t> lastResult{0};
std::atomic<uint32_t> runtimeStatus{0};
std::atomic<bool> dynamicActive{false};
std::atomic<uint32_t> framesPresentedInSample{0};
std::atomic<uint32_t> stateVersion{2};
std::atomic<uint64_t> lastStateProbe{0};

std::atomic<bool> mfgActive{false};
std::atomic<bool> markersActive{false};
std::atomic<uint32_t> markerCount{0};
std::atomic<uint64_t> lastMarkerTick{0};
std::atomic<uint32_t> reflexSleepCount{0};
std::atomic<uint32_t> appliedQueueParallelismMode{0};

std::mutex presentationMutex;
std::recursive_mutex optionsMutex;
std::mutex reflexMutex;
std::mutex loggingMutex;

sl::Result HookGetState(const sl::ViewportHandle&, sl::DLSSGState&, const sl::DLSSGOptions*);

void ObservePresentedFrames(uint32_t presented) {
    std::lock_guard lock(presentationMutex);
    static uint64_t lastLog = 0;
    const uint64_t now = GetTickCount64();
    framesPresentedInSample.store(presented);
    if (now - lastLog < 1000) return;
    lastLog = now;
    NRF_LOG_INFO("StreamlinePresentation", "SDK presented sample=%u status=%u optionsResult=%u",
                 presented, runtimeStatus.load(), lastResult.load());
}

size_t OptionsBytes(size_t version) {
    if (version >= 5) return sizeof(sl::DLSSGOptions);
    size_t end = offsetof(sl::DLSSGOptions, bReserved15);
    if (version == 2) end = offsetof(sl::DLSSGOptions, bReserved15) + sizeof(sl::Boolean);
    if (version == 3) end = offsetof(sl::DLSSGOptions, queueParallelismMode) + sizeof(sl::DLSSGQueueParallelismMode);
    if (version == 4) end = offsetof(sl::DLSSGOptions, enableUserInterfaceRecomposition) + sizeof(sl::Boolean);
    const size_t alignment = alignof(sl::DLSSGOptions);
    return (end + alignment - 1) & ~(alignment - 1);
}

void LogOptions(const sl::DLSSGOptions& requested, const sl::DLSSGOptions& applied, sl::Result result) {
    std::lock_guard lock(loggingMutex);
    static uint32_t prevMode = UINT32_MAX, prevFrames = UINT32_MAX, prevResult = UINT32_MAX, prevQueue = UINT32_MAX;
    const uint32_t mode = static_cast<uint32_t>(applied.mode);
    const uint32_t queue = static_cast<uint32_t>(applied.queueParallelismMode);
    const uint32_t ret = static_cast<uint32_t>(result);
    if (prevMode == mode && prevFrames == applied.numFramesToGenerate && prevResult == ret && prevQueue == queue) return;
    prevMode = mode; prevFrames = applied.numFramesToGenerate; prevResult = ret; prevQueue = queue;
    NRF_LOG_INFO("StreamlineHook", "MFG options v%zu mode=%u->%u generated=%u queue=%u result=%u",
        applied.structVersion, static_cast<uint32_t>(requested.mode), mode, applied.numFramesToGenerate, queue, ret);
}

sl::Result HookReflexSetOptions(const sl::ReflexOptions& requested) {
    std::lock_guard lock(reflexMutex);
    auto original = originalReflexSetOptions.load(std::memory_order_relaxed);
    if (!original) return sl::Result::eErrorNotInitialized;
    const auto applied = StreamlineReflexTracker::Instance().OnGameReflexSetOptions(
        requested, mfgActive.load(std::memory_order_relaxed));
    return original(applied);
}

void DispatchReflexOptionsIfPending() {
    sl::ReflexOptions reflexOptions{};
    if (StreamlineReflexTracker::Instance().ApplyCurrentLatencyPolicy(
            mfgActive.load(std::memory_order_relaxed), reflexOptions)) {
        auto reflexFunc = originalReflexSetOptions.load(std::memory_order_relaxed);
        if (!reflexFunc) {
            auto dispatcher = featureDispatcher.load(std::memory_order_relaxed);
            if (dispatcher) {
                void* target = nullptr;
                if (dispatcher(static_cast<uint32_t>(sl::kFeatureReflex), "slReflexSetOptions", &target) == 0 && target) {
                    reflexFunc = reinterpret_cast<SetReflexOptions>(target);
                    originalReflexSetOptions.store(reflexFunc, std::memory_order_release);
                }
            }
        }
        if (reflexFunc) reflexFunc(reflexOptions);
    }
}

sl::Result HookReflexSleep(const sl::FrameToken& frame) {
    reflexSleepCount.fetch_add(1, std::memory_order_relaxed);
    StreamlineReflexTracker::Instance().RecordSleep(frame);
    DispatchReflexOptionsIfPending();
    auto original = originalReflexSleep.load(std::memory_order_relaxed);
    if (!original) return sl::Result::eErrorNotInitialized;
    return original(frame);
}

sl::Result HookPclSetMarker(sl::PCLMarker marker, const sl::FrameToken& frame) {
    lastMarkerTick.store(GetTickCount64(), std::memory_order_relaxed);
    markerCount.fetch_add(1, std::memory_order_relaxed);
    markersActive.store(true, std::memory_order_relaxed);
    StreamlineReflexTracker::Instance().RecordMarker(marker, frame);
    auto original = originalPclSetMarker.load(std::memory_order_relaxed);
    if (!original) return sl::Result::eErrorNotInitialized;
    return original(marker, frame);
}

sl::Result HookSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& requested) {
    std::lock_guard lock(optionsMutex);
    const auto original = originalSetOptions.load();
    if (!original) return sl::Result::eErrorNotInitialized;
    if (GetSafeReadableSpan(&requested, sizeof(sl::BaseStructure)) != sizeof(sl::BaseStructure) ||
        requested.structVersion == 0 || requested.structVersion > sl::kStructVersion5)
        return original(viewport, requested);
    const size_t bytes = OptionsBytes(requested.structVersion);
    if (GetSafeReadableSpan(&requested, bytes) != bytes) return original(viewport, requested);
    sl::DLSSGOptions applied{};
    std::memcpy(&applied, &requested, bytes);
    optionsVersion.store(static_cast<uint32_t>(requested.structVersion));
    auto& generation = DlssgTransfusion::Instance();
    uint32_t mode = static_cast<uint32_t>(requested.mode);
    uint32_t frames = requested.numFramesToGenerate;
    generation.ProcessSetOptions(mode, frames);
    applied.mode = static_cast<sl::DLSSGMode>(mode);
    applied.numFramesToGenerate = frames;
    if (applied.mode == sl::DLSSGMode::eOff)
        applied.numFramesToGenerate = std::max(1u, requested.numFramesToGenerate);
    if (generation.GetControlMode() == MfgControlMode::Dynamic && dynamicSupported.load() && requested.structVersion >= 5) {
        applied.mode = sl::DLSSGMode::eDynamic;
        applied.numFramesToGenerate = std::max(1u, std::min(maximumFrames.load(), generation.AutomaticMultiplierLimit() - 1u));
        applied.dynamicTargetFrameRate = static_cast<float>(generation.GetDynamicTargetFps());
    }
    if (generation.GetControlMode() != MfgControlMode::FollowGame)
        applied.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
    const uint32_t maximum = maximumFrames.load();
    if (maximum && applied.mode != sl::DLSSGMode::eOff)
        applied.numFramesToGenerate = std::clamp(applied.numFramesToGenerate, 1u, maximum);
    const bool isMfgActive = (applied.mode != sl::DLSSGMode::eOff);
    mfgActive.store(isMfgActive, std::memory_order_relaxed);
    if (isMfgActive) {
        if (applied.structVersion < sl::kStructVersion3) applied.structVersion = sl::kStructVersion3;
        applied.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
        appliedQueueParallelismMode.store(static_cast<uint32_t>(applied.queueParallelismMode), std::memory_order_relaxed);
    }
    StreamlineReflexTracker::Instance().SetMfgMultiplier(applied.numFramesToGenerate + 1);
    DispatchReflexOptionsIfPending();
    const auto result = original(viewport, applied);
    lastResult.store(static_cast<uint32_t>(result));
    if (result == sl::Result::eOk || result == sl::Result::eWarnOutOfVRAM) {
        dynamicActive.store(applied.mode == sl::DLSSGMode::eDynamic);
        updatePending.store(applied.mode != sl::DLSSGMode::eDynamic && generation.TransitionPending());
        generation.ObserveAcceptedOptions(static_cast<uint32_t>(applied.mode), applied.numFramesToGenerate);
        if (applied.mode != sl::DLSSGMode::eOff && result == sl::Result::eWarnOutOfVRAM &&
            generation.ObserveVramWarning(applied.numFramesToGenerate)) {
            updatePending.store(true);
            NRF_LOG_WARN("StreamlineHook", "MFG Auto VRAM warning: limiting multiplier to %ux", generation.AutomaticMultiplierLimit());
        }
    }
    LogOptions(requested, applied, result);
    const uint64_t now = GetTickCount64();
    uint64_t prior = lastStateProbe.load();
    if (now - prior >= 1000 && lastStateProbe.compare_exchange_strong(prior, now)) {
        sl::DLSSGState observed{};
        observed.structVersion = std::min<size_t>(stateVersion.load(), 4);
        HookGetState(viewport, observed, nullptr);
    }
    return result;
}

sl::Result HookGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state, const sl::DLSSGOptions* options) {
    std::lock_guard lock(optionsMutex);
    const auto original = originalGetState.load();
    if (!original) return sl::Result::eErrorNotInitialized;
    const auto result = original(viewport, state, options);
    if (result != sl::Result::eOk && result != sl::Result::eWarnOutOfVRAM) return result;
    stateVersion.store(static_cast<uint32_t>(state.structVersion));
    runtimeStatus.store(static_cast<uint32_t>(state.status));
    ObservePresentedFrames(state.numFramesActuallyPresented);
    if (state.structVersion >= 2) {
        auto& generation = DlssgTransfusion::Instance();
        generation.ProcessGetState(state.numFramesToGenerateMax);
        generation.ObserveGenerationLimit(state.numFramesToGenerateMax);
        const uint32_t prior = maximumFrames.exchange(state.numFramesToGenerateMax);
        if (prior != state.numFramesToGenerateMax)
            NRF_LOG_INFO("StreamlineHook", "MFG runtime maxGenerated=%u stateVersion=%zu status=%u",
                         state.numFramesToGenerateMax, state.structVersion, static_cast<uint32_t>(state.status));
    }
    if (state.structVersion >= 4)
        dynamicSupported.store(state.bIsDynamicMFGSupported == sl::Boolean::eTrue);
    return result;
}
} // namespace

void SetFeatureDispatcher(void* dispatcher) noexcept {
    featureDispatcher.store(reinterpret_cast<GetFeatureFunction>(dispatcher), std::memory_order_release);
}

void InterceptDlssgFunction(const char* name, void** function) noexcept {
    if (std::strcmp(name, "slDLSSGSetOptions") == 0 && *function != reinterpret_cast<void*>(&HookSetOptions)) {
        originalSetOptions.store(reinterpret_cast<SetOptions>(*function));
        *function = reinterpret_cast<void*>(&HookSetOptions);
    } else if (std::strcmp(name, "slDLSSGGetState") == 0 && *function != reinterpret_cast<void*>(&HookGetState)) {
        originalGetState.store(reinterpret_cast<GetState>(*function));
        *function = reinterpret_cast<void*>(&HookGetState);
    }
}

void InterceptReflexFunction(const char* name, void** function) noexcept {
    if (std::strcmp(name, "slReflexSetOptions") == 0 && *function != reinterpret_cast<void*>(&HookReflexSetOptions)) {
        originalReflexSetOptions.store(reinterpret_cast<SetReflexOptions>(*function));
        *function = reinterpret_cast<void*>(&HookReflexSetOptions);
    } else if (std::strcmp(name, "slReflexSleep") == 0 && *function != reinterpret_cast<void*>(&HookReflexSleep)) {
        originalReflexSleep.store(reinterpret_cast<ReflexSleep>(*function));
        *function = reinterpret_cast<void*>(&HookReflexSleep);
    } else if (std::strcmp(name, "slReflexSetMarker") == 0 && *function != reinterpret_cast<void*>(&HookPclSetMarker)) {
        originalPclSetMarker.store(reinterpret_cast<SetPclMarker>(*function));
        *function = reinterpret_cast<void*>(&HookPclSetMarker);
    }
}

void InterceptPclFunction(const char* name, void** function) noexcept {
    if (std::strcmp(name, "slPCLSetMarker") == 0 && *function != reinterpret_cast<void*>(&HookPclSetMarker)) {
        originalPclSetMarker.store(reinterpret_cast<SetPclMarker>(*function));
        *function = reinterpret_cast<void*>(&HookPclSetMarker);
    }
}

void RequestOptionsUpdate() noexcept {
    updatePending.store(true);
    DispatchReflexOptionsIfPending();
}

StreamlineMfgStatus ReadMfgStatus() noexcept {
    return {originalSetOptions.load() != nullptr && originalGetState.load() != nullptr, dynamicSupported.load(),
            updatePending.load(), optionsVersion.load(), maximumFrames.load(), lastResult.load(), runtimeStatus.load(),
            dynamicActive.load(), framesPresentedInSample.load(), originalReflexSetOptions.load() != nullptr,
            markersActive.load(), markerCount.load(), appliedQueueParallelismMode.load()};
}

static_assert(sizeof(void*) != 8 || offsetof(sl::DLSSGOptions, mode) == 32);
static_assert(sizeof(void*) != 8 || offsetof(sl::DLSSGOptions, numFramesToGenerate) == 36);
static_assert(sizeof(void*) != 8 || offsetof(sl::DLSSGState, numFramesToGenerateMax) == 52);
} // namespace nrfusion::streamline
