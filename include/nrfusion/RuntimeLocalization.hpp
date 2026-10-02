#pragma once

#include <cstdint>

namespace nrfusion {

enum class UiLanguage : std::uint8_t {
    English = 0,
    Portuguese = 1,
};

struct UiStrings {
    const wchar_t* title;
    const wchar_t* subtitle;
    const wchar_t* tabMain;
    const wchar_t* tabAdvanced;
    const wchar_t* enabledOn;
    const wchar_t* enabledOff;
    const wchar_t* nrModeTitle;
    const wchar_t* nrModeAuto;
    const wchar_t* nrModeQuality;
    const wchar_t* nrModePerf;
    const wchar_t* nrModeCustom;
    const wchar_t* targetFpsPrefix;
    const wchar_t* displayHzPrefix;
    const wchar_t* mfgModeTitle;
    const wchar_t* mfgFollowGame;
    const wchar_t* mfgFixed;
    const wchar_t* mfgDynamic;
    const wchar_t* statusTitle;
    const wchar_t* statusNrActive;
    const wchar_t* statusNrInactive;
    const wchar_t* statusOutput;
    const wchar_t* btnApply;
    const wchar_t* neuralPrecision;
    const wchar_t* pipelinePlacement;
    const wchar_t* residual;
    const wchar_t* multipass;
    const wchar_t* mfg56x;
    const wchar_t* diagnostics;
    const wchar_t* langButton;
    const char* toastWelcome;
    const char* toastSaved;
    const char* toastFailed;
};

class RuntimeLocalization {
public:
    static UiLanguage DetectSystemLanguage() noexcept;
    static void SetLanguage(UiLanguage lang) noexcept;
    static UiLanguage GetLanguage() noexcept;
    static void ToggleLanguage() noexcept;
    static const UiStrings& Strings() noexcept;
};

} // namespace nrfusion
