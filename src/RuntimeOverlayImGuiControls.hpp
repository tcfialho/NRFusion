#pragma once
#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeLocalization.hpp"
#include <imgui.h>

namespace nrfusion {
inline const char* MenuLabel(const char* english, const char* portuguese) {
    return RuntimeLocalization::GetLanguage() == UiLanguage::Portuguese ? portuguese : english;
}
inline void MenuHelp(const char* english, const char* portuguese) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34);
        ImGui::TextUnformatted(MenuLabel(english, portuguese));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
bool DrawMfgMode(RuntimeConfig& config);
bool DrawManualMfgMultiplier(RuntimeConfig& config, RuntimeAdvancedConfig& advanced);
void DrawRuntimeOverlayAdvanced(RuntimeOverlay& overlay);
} // namespace nrfusion
