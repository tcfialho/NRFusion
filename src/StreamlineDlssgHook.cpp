#include "nrfusion/StreamlineDlssgHook.hpp"
#include "nrfusion/Logger.hpp"
#include "StreamlineDlssgOptions.hpp"
#include "StreamlineReflexTracker.hpp"
#include <MinHook.h>
#include <sl_core_types.h>
#include <mutex>

namespace nrfusion {
namespace {
using GetFeatureFunction = uint32_t(*)(uint32_t, const char*, void**);
GetFeatureFunction originalGetFeatureFunction = nullptr;
std::mutex installMutex;

uint32_t HookGetFeatureFunction(uint32_t feature, const char* name, void** function) {
    const uint32_t result = originalGetFeatureFunction(feature, name, function);
    if (result == 0 && function && *function && name) {
        if (feature == static_cast<uint32_t>(sl::kFeatureDLSS_G))
            streamline::InterceptDlssgFunction(name, function);
        else if (feature == static_cast<uint32_t>(sl::kFeatureReflex))
            streamline::InterceptReflexFunction(name, function);
        else if (feature == static_cast<uint32_t>(sl::kFeaturePCL))
            streamline::InterceptPclFunction(name, function);
    }
    return result;
}
} // namespace

StreamlineDlssgHook& StreamlineDlssgHook::Instance() noexcept {
    static StreamlineDlssgHook instance;
    return instance;
}

bool StreamlineDlssgHook::Install(HMODULE interposerModule) noexcept {
    std::lock_guard lock(installMutex);
    if (installed_.load()) return true;
    HMODULE interposer = interposerModule ? interposerModule : GetModuleHandleW(L"sl.interposer.dll");
    if (!interposer) return false;
    void* target = reinterpret_cast<void*>(GetProcAddress(interposer, "slGetFeatureFunction"));
    if (!target) return false;
    const auto initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED) return false;
    auto status = MH_CreateHook(target, reinterpret_cast<void*>(&HookGetFeatureFunction),
                               reinterpret_cast<void**>(&originalGetFeatureFunction));
    if (status != MH_OK) {
        NRF_LOG_ERROR("StreamlineHook", "Feature dispatcher hook failed: %s", MH_StatusToString(status));
        return false;
    }
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(target), &pinned)) {
        MH_RemoveHook(target);
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        MH_RemoveHook(target);
        NRF_LOG_ERROR("StreamlineHook", "Feature dispatcher activation failed: %s", MH_StatusToString(status));
        return false;
    }
    installed_.store(true);
    streamline::SetFeatureDispatcher(reinterpret_cast<void*>(originalGetFeatureFunction));
    NRF_LOG_INFO("StreamlineHook", "Versioned Streamline interception installed");
    return true;
}

void StreamlineDlssgHook::TriggerLiveMultiplierUpdate() noexcept { streamline::RequestOptionsUpdate(); }
StreamlineMfgStatus StreamlineDlssgHook::Status() const noexcept { return streamline::ReadMfgStatus(); }

ReflexOwnershipInfo StreamlineDlssgHook::ReflexOwnership() const noexcept {
    return streamline::StreamlineReflexTracker::Instance().GetOwnershipInfo();
}

ReflexValidationStats StreamlineDlssgHook::ReflexStats() const noexcept {
    return streamline::StreamlineReflexTracker::Instance().GetValidationStats();
}

void StreamlineDlssgHook::ResetReflexValidation() noexcept {
    streamline::StreamlineReflexTracker::Instance().Reset();
}

} // namespace nrfusion
