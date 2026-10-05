#include "RuntimeOverlayD3D12.hpp"
#include "RuntimeOverlayImGuiControls.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/StreamlineDlssgHook.hpp"
#include <array>

namespace nrfusion {
bool DrawMfgMode(RuntimeConfig& config) {
    constexpr std::array modes{RuntimeMfgMode::Dynamic, RuntimeMfgMode::Off, RuntimeMfgMode::FollowGame, RuntimeMfgMode::Fixed};
    const char* names[] = {MenuLabel("Auto", "Automático"), MenuLabel("Off", "Desligado"),
                           MenuLabel("Follow game", "Seguir o jogo"), MenuLabel("Manual", "Manual")};
    int selected = 0;
    for (int index = 0; index < 4; ++index) if (config.mfgMode == modes[index]) selected = index;
    bool changed = false;
    if (ImGui::BeginCombo(MenuLabel("Frame generation", "Geração de quadros"), names[selected])) {
        for (int index = 0; index < 4; ++index) {
            if (ImGui::Selectable(names[index], selected == index)) { config.mfgMode = modes[index]; changed = true; }
        }
        ImGui::EndCombo();
    }
    MenuHelp("Independent from NR. Auto follows the display target; Manual shows its multiplier here.",
             "Independente do NR. Automático segue a meta do monitor; Manual mostra o multiplicador aqui.");
    return changed;
}

namespace {
void DrawMain(RuntimeOverlay& overlay) {
    auto main = overlay.MenuDrawing().MainDraft();
    auto advanced = overlay.MenuDrawing().AdvancedDraft();
    bool mainChanged = ImGui::Checkbox(MenuLabel("Neural rendering", "Filtro neural"), &main.enabled);
    MenuHelp("Turn off the neural model while keeping frame generation independently configured.",
             "Desliga o modelo neural; a geração de quadros continua configurada separadamente.");
    const char* modes[] = {MenuLabel("Auto", "Automático"), MenuLabel("Best quality", "Qualidade"),
                           MenuLabel("Performance", "Desempenho"), MenuLabel("Manual", "Manual")};
    bool advancedChanged = false;
    int mode = static_cast<int>(main.mode);
    const bool modeChanged = ImGui::Combo(MenuLabel("NR mode", "Modo NR"), &mode, modes, 4);
    mainChanged |= modeChanged;
    if (modeChanged && mode == static_cast<int>(RuntimeNrMode::Custom)) {
        advanced.nr.workingScale = overlay.MenuDrawing().StatusSnapshot().workingScale;
        advancedChanged = true;
    }
    main.mode = static_cast<RuntimeNrMode>(mode);
    MenuHelp("Auto measures completed GPU work and adjusts neural resolution. Quality uses full resolution.",
             "Automático mede o trabalho concluído na GPU e ajusta a resolução neural. Qualidade usa resolução completa.");
    if (main.mode == RuntimeNrMode::Custom) {
        float percent = advanced.nr.workingScale * 100;
        if (ImGui::SliderFloat(MenuLabel("Neural resolution", "Resolução neural"), &percent, 25, 100, "%.0f%%")) {
            advanced.nr.workingScale = percent / 100;
            advancedChanged = true;
        }
    }
    const char* profiles[] = {MenuLabel("Standard", "Padrão"), MenuLabel("Natural", "Natural"), MenuLabel("Cinematic", "Cinemático")};
    int style = static_cast<int>(advanced.nr.appearance.style);
    advancedChanged |= ImGui::Combo(MenuLabel("Appearance", "Aparência"), &style, profiles, 3);
    advanced.nr.appearance.style = static_cast<Dlss5Style>(style);
    MenuHelp("Changes the appearance profile of the actual neural model.",
             "Muda o perfil visual do modelo neural que processa a imagem.");
    ImGui::Separator();
    mainChanged |= DrawMfgMode(main);
    if (main.mfgMode == RuntimeMfgMode::Fixed) advancedChanged |= DrawManualMfgMultiplier(main, advanced);
    if (mainChanged || advancedChanged) overlay.MenuDrawing().StageMain(main);
    if (advancedChanged) overlay.MenuDrawing().StageAdvanced(advanced);
}

void DrawStatus(RuntimeOverlay& overlay) {
    const auto status = overlay.MenuDrawing().StatusSnapshot();
    const auto mfg = StreamlineDlssgHook::Instance().Status();
    ImGui::Separator();
    ImGui::TextUnformatted(status.nrActive ? MenuLabel("NR active", "NR ativo") : MenuLabel("NR inactive", "NR inativo"));
    ImGui::SameLine();
    const bool accepted = mfg.lastResult == 0 || mfg.lastResult == 39;
    if (mfg.linked && accepted && mfg.dynamicActive) ImGui::TextUnformatted(" | MFG: Auto");
    else if (mfg.linked && accepted) ImGui::Text(" | MFG: %uX", status.effectiveMultiplier);
    else ImGui::TextUnformatted(MenuLabel(" | MFG unavailable", " | MFG indisponível"));
    if (mfg.updatePending) ImGui::TextUnformatted(MenuLabel("MFG: waiting for the game update", "MFG: aguardando atualização do jogo"));
    if (mfg.lastResult == 39) ImGui::TextColored(ImVec4(1, .7f, .2f, 1), "%s",
        MenuLabel("MFG: video-memory warning", "MFG: aviso de memória de vídeo"));
    else if (mfg.linked && mfg.lastResult != 0)
        ImGui::Text(MenuLabel("MFG rejected the request (%u)", "MFG recusou a solicitação (%u)"), mfg.lastResult);
    const auto apply = overlay.LastConfigurationResult();
    if (apply == ConfigurationResult::Invalid || apply == ConfigurationResult::Failed)
        ImGui::TextColored(ImVec4(1, .4f, .3f, 1), "%s", MenuLabel("Settings were not applied.", "As opções não foram aplicadas."));
    if (apply == ConfigurationResult::AppliedSaveFailed)
        ImGui::TextColored(ImVec4(1, .7f, .2f, 1), "%s", MenuLabel("Applied; saving failed.", "Aplicado; falhou ao salvar."));
}
} // namespace

void DrawRuntimeOverlayImGui(bool standaloneWindow) {
    auto& overlay = RuntimeOverlay::Instance();
    if (!overlay.IsMenuOpen()) return;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (standaloneWindow) {
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
    } else {
        ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(540, 440), ImGuiCond_FirstUseEver);
    }
    bool open = true;
    if (ImGui::Begin("NRFUSION", &open, flags)) {
        ImGui::TextUnformatted(MenuLabel("Automatic by default", "Automático por padrão"));
        ImGui::SameLine();
        if (ImGui::Button(RuntimeLocalization::GetLanguage() == UiLanguage::Portuguese ? "EN" : "PT"))
            RuntimeLocalization::ToggleLanguage();
        if (ImGui::BeginTabBar("NRFusionTabs")) {
            if (ImGui::BeginTabItem(MenuLabel("Main", "Principal"))) { DrawMain(overlay); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(MenuLabel("Advanced", "Avançado"))) { DrawRuntimeOverlayAdvanced(overlay); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        DrawStatus(overlay);
        if (ImGui::Button(MenuLabel("Apply and save", "Aplicar e salvar"), ImVec2(-1, 30))) overlay.ApplyStagedConfiguration();
        ImGui::TextUnformatted(MenuLabel("Insert / F8: menu | Esc: close | Enter: apply", "Insert / F8: menu | Esc: fechar | Enter: aplicar"));
    }
    overlay.SetEditingText(ImGui::GetIO().WantTextInput);
    ImGui::End();
    if (!open) overlay.CloseMenu();
}
} // namespace nrfusion
