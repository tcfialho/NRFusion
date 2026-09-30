#include "StreamlineDlssgOptions.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/Logger.hpp"
#include "PeMemoryUtils.hpp"
#include <sl_dlss_g.h>
#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <mutex>

namespace nrfusion::streamline {
namespace {
using SetOptions = sl::Result(*)(const sl::ViewportHandle&, const sl::DLSSGOptions&);
using GetState = sl::Result(*)(const sl::ViewportHandle&, sl::DLSSGState&, const sl::DLSSGOptions*);
std::atomic<SetOptions> originalSetOptions{nullptr};
std::atomic<GetState> originalGetState{nullptr};
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
std::mutex presentationMutex;
std::recursive_mutex optionsMutex;

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
std::mutex loggingMutex;

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
    static uint32_t previousMode = UINT32_MAX, previousFrames = UINT32_MAX, previousResult = UINT32_MAX;
    static float previousTarget = -1;
    const uint32_t mode = static_cast<uint32_t>(applied.mode);
    const uint32_t returned = static_cast<uint32_t>(result);
    if (previousMode == mode && previousFrames == applied.numFramesToGenerate && previousResult == returned &&
        previousTarget == applied.dynamicTargetFrameRate) return;
    previousMode = mode;
    previousFrames = applied.numFramesToGenerate;
    previousResult = returned;
    previousTarget = applied.dynamicTargetFrameRate;
    NRF_LOG_INFO("StreamlineHook", "MFG options v%zu mode=%u->%u generated=%u->%u target=%.1f result=%u",
        requested.structVersion, static_cast<uint32_t>(requested.mode), mode, requested.numFramesToGenerate,
        applied.numFramesToGenerate, applied.dynamicTargetFrameRate, returned);
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
        applied.numFramesToGenerate = std::max(1u, maximumFrames.load());
        applied.dynamicTargetFrameRate = static_cast<float>(generation.GetDynamicTargetFps());
    }
    if (generation.GetControlMode() != MfgControlMode::FollowGame)
        applied.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
    const uint32_t maximum = maximumFrames.load();
    if (maximum && applied.mode != sl::DLSSGMode::eOff)
        applied.numFramesToGenerate = std::clamp(applied.numFramesToGenerate, 1u, maximum);
    const auto result = original(viewport, applied);
    lastResult.store(static_cast<uint32_t>(result));
    if (result == sl::Result::eOk || result == sl::Result::eWarnOutOfVRAM) {
        dynamicActive.store(applied.mode == sl::DLSSGMode::eDynamic);
        updatePending.store(applied.mode != sl::DLSSGMode::eDynamic && generation.TransitionPending());
        generation.ObserveAcceptedOptions(static_cast<uint32_t>(applied.mode), applied.numFramesToGenerate);
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

void InterceptDlssgFunction(const char* name, void** function) noexcept {
    if (std::strcmp(name, "slDLSSGSetOptions") == 0 && *function != reinterpret_cast<void*>(&HookSetOptions)) {
        originalSetOptions.store(reinterpret_cast<SetOptions>(*function));
        *function = reinterpret_cast<void*>(&HookSetOptions);
    } else if (std::strcmp(name, "slDLSSGGetState") == 0 && *function != reinterpret_cast<void*>(&HookGetState)) {
        originalGetState.store(reinterpret_cast<GetState>(*function));
        *function = reinterpret_cast<void*>(&HookGetState);
    }
}

void RequestOptionsUpdate() noexcept { updatePending.store(true); }

StreamlineMfgStatus ReadMfgStatus() noexcept {
    return {originalSetOptions.load() != nullptr && originalGetState.load() != nullptr, dynamicSupported.load(),
            updatePending.load(), optionsVersion.load(), maximumFrames.load(), lastResult.load(), runtimeStatus.load(), dynamicActive.load(), framesPresentedInSample.load()};
}

static_assert(sizeof(void*) != 8 || offsetof(sl::DLSSGOptions, mode) == 32);
static_assert(sizeof(void*) != 8 || offsetof(sl::DLSSGOptions, numFramesToGenerate) == 36);
static_assert(sizeof(void*) != 8 || offsetof(sl::DLSSGState, numFramesToGenerateMax) == 52);
} // namespace nrfusion::streamline
