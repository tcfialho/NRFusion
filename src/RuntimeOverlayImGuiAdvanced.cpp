#include "RuntimeOverlayImGuiControls.hpp"
#include "nrfusion/StreamlineDlssgHook.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include <array>
#include <algorithm>

namespace nrfusion {
namespace {
bool DrawPrecision(RuntimeAdvancedConfig& config) {
    const char* labels[] = {"Auto (NVIDIA FP8)", "NVIDIA FP8", "FP16", "INT8", "FP8 + NVFP4"};
    const int selected = config.nr.precisionAuto ? 0 : 1;
    bool changed = false;
    if (ImGui::BeginCombo(MenuLabel("Model precision", "Precisão do modelo"), labels[selected])) {
        for (int index = 0; index < 5; ++index) {
            ImGui::BeginDisabled(index > 1);
            if (ImGui::Selectable(labels[index], index == selected)) {
                config.nr.precisionAuto = index == 0;
                config.nr.precision = NrPrecision::Fp8;
                changed = true;
            }
            ImGui::EndDisabled();
            if (index > 1) MenuHelp("This NVIDIA route exposes FP8. FP16/INT8 are not model variants; the experimental hybrid is not integrated here.",
                "Esta rota NVIDIA usa FP8. FP16/INT8 não são variantes do modelo; o híbrido experimental não está integrado aqui.");
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawPlacement(RuntimeAdvancedConfig& config, const MenuStatusSnapshot& status) {
    constexpr std::array placements{NrPlacement::Auto, NrPlacement::PreSr, NrPlacement::PostSr};
    const char* names[] = {MenuLabel("Auto (before DLSS)", "Auto (antes do DLSS)"),
                          MenuLabel("Before DLSS", "Antes do DLSS"), MenuLabel("After DLSS", "Depois do DLSS")};
    int selected = 0;
    for (int index = 0; index < 3; ++index) if (config.nr.placement == placements[index]) selected = index;
    bool changed = false;
    if (ImGui::BeginCombo(MenuLabel("NR placement", "Posição do NR"), names[selected])) {
        for (int index = 0; index < 3; ++index)
            if (ImGui::Selectable(names[index], index == selected)) { config.nr.placement = placements[index]; changed = true; }
        ImGui::EndCombo();
    }
    MenuHelp("Before: processes the lower-resolution DLSS input. After: processes the upscaled image. Before RR is experimental.",
             "Antes: processa a entrada de menor resolução do DLSS. Depois: processa a imagem ampliada. Antes do RR é experimental.");
    const bool residualAvailable = status.rayReconstruction &&
        (config.nr.placement == NrPlacement::Auto || config.nr.placement == NrPlacement::PreSr || config.nr.placement == NrPlacement::AcrossRr);
    ImGui::BeginDisabled(!residualAvailable);
    changed |= ImGui::Checkbox(MenuLabel("Carry NR edit across Ray Reconstruction", "Preservar efeito através do Ray Reconstruction"), &config.nr.residualEnabled);
    ImGui::EndDisabled();
    MenuHelp("Runs NR before RR, then adds only its difference to the RR output. Requires active RR and before-DLSS placement.",
             "Executa NR antes do RR e soma apenas sua diferença ao resultado do RR. Exige RR ativo e posição antes do DLSS.");
    if (config.nr.residualEnabled && residualAvailable)
        changed |= ImGui::SliderFloat(MenuLabel("Detail accumulation", "Acúmulo de detalhe"), &config.nr.residualBlend, .01f, 1, "%.2f");
    return changed;
}

void DrawNeuralOptions(RuntimeOverlay& overlay) {
    auto main = overlay.MenuDrawing().MainDraft();
    auto config = overlay.MenuDrawing().AdvancedDraft();
    const auto status = overlay.MenuDrawing().StatusSnapshot();
    bool mainChanged = ImGui::SliderFloat(MenuLabel("Target rendered FPS", "Meta de FPS renderizados"), &main.targetFps, 30, 240, "%.0f FPS");
    bool manual = main.mode == RuntimeNrMode::Custom;
    if (ImGui::Checkbox(MenuLabel("Manual neural resolution", "Resolução neural manual"), &manual)) {
        main.mode = manual ? RuntimeNrMode::Custom : RuntimeNrMode::Auto;
        if (manual) config.nr.workingScale = status.workingScale;
        mainChanged = true;
    }
    ImGui::BeginDisabled(!manual);
    float percent = manual ? config.nr.workingScale * 100 : status.workingScale * 100;
    bool changed = ImGui::SliderFloat(MenuLabel("Neural resolution", "Resolução neural"), &percent, 25, 100, "%.0f%%");
    if (changed) config.nr.workingScale = percent / 100;
    ImGui::EndDisabled();
    MenuHelp("Base rendering stays at game resolution. Reduced NR runs cheaper and recomposes its edit onto the full image.",
             "A imagem base mantém a resolução do jogo. NR reduzido custa menos e recompõe seu efeito na imagem completa.");
    changed |= DrawPlacement(config, status);
    changed |= DrawPrecision(config);
    changed |= ImGui::Checkbox(MenuLabel("Multiple neural passes", "Múltiplas passagens neurais"), &config.nr.multipassEnabled);
    MenuHelp("Runs the model repeatedly. Each pass uses the selected appearance profile and adds GPU and memory cost. Default: one pass.",
             "Executa o modelo repetidamente. Cada passagem usa o perfil escolhido e aumenta o custo de GPU e memória. Padrão: uma passagem.");
    if (config.nr.multipassEnabled) {
        int count = static_cast<int>(config.nr.passCount);
        if (ImGui::SliderInt(MenuLabel("Pass count", "Passagens"), &count, 2, 3)) { config.nr.passCount = count; changed = true; }
    }
    if (ImGui::CollapsingHeader(MenuLabel("Appearance strength", "Intensidade do perfil"))) {
        changed |= ImGui::SliderFloat(MenuLabel("Intensity", "Intensidade"), &config.nr.appearance.intensity, 0, 2, "%.2f");
        changed |= ImGui::SliderFloat(MenuLabel("Local structure", "Estrutura local"), &config.nr.appearance.localStructure, 0, 2, "%.2f");
    }
    if (mainChanged) overlay.MenuDrawing().StageMain(main);
    if (changed || mainChanged) overlay.MenuDrawing().StageAdvanced(config);
}

void DrawGenerationOptions(RuntimeOverlay& overlay) {
    ImGui::PushID("advanced_generation_options");
    auto main = overlay.MenuDrawing().MainDraft();
    auto advanced = overlay.MenuDrawing().AdvancedDraft();
    const auto runtime = StreamlineDlssgHook::Instance().Status();
    bool changed = DrawMfgMode(main);
    if (main.mfgMode == RuntimeMfgMode::Fixed) changed |= DrawManualMfgMultiplier(main, advanced);
    changed |= ImGui::Checkbox(MenuLabel("Auto output FPS (monitor)", "FPS de saída automático (monitor)"), &main.displayHzAuto);
    changed |= ImGui::Checkbox(MenuLabel("Allow 5X/6X in Auto", "Permitir 5X/6X no Automático"), &advanced.mfg.allowExperimental56x);
    if (!main.displayHzAuto)
        changed |= ImGui::SliderFloat(MenuLabel("Output target", "Meta de saída"), &main.displayHz, 30, 360, "%.0f FPS");
    MenuHelp("Output FPS includes generated frames. This target is separate from NR's rendered-FPS budget.",
             "FPS de saída inclui os quadros gerados. Esta meta é separada do orçamento de FPS renderizados do NR.");
    if (runtime.linked && !runtime.nativeDynamicSupported)
        ImGui::TextWrapped("%s", MenuLabel("Auto uses NRFusion's stable multiplier selection; native Dynamic is unavailable.",
            "Automático usa a seleção estável de multiplicador do NRFusion; Dynamic nativo está indisponível."));
    if (changed) {
        overlay.MenuDrawing().StageMain(main);
        overlay.MenuDrawing().StageAdvanced(advanced);
    }
    ImGui::PopID();
}
} // namespace

bool DrawManualMfgMultiplier(RuntimeConfig& config, RuntimeAdvancedConfig& advanced) {
    const auto runtime = StreamlineDlssgHook::Instance().Status();
    bool changed = false;
    const std::string selected = std::to_string(config.mfgMultiplier) + "X";
    if (ImGui::BeginCombo(MenuLabel("Multiplier", "Multiplicador"), selected.c_str())) {
        for (unsigned multiplier = 2; multiplier <= 6; ++multiplier) {
            const bool supported = runtime.linked && multiplier <= runtime.maxGeneratedFrames + 1;
            ImGui::BeginDisabled(!supported);
            const auto name = std::to_string(multiplier) + "X";
            if (ImGui::Selectable(name.c_str(), config.mfgMultiplier == multiplier)) {
                config.mfgMultiplier = static_cast<std::uint8_t>(multiplier);
                if (multiplier > 4) advanced.mfg.allowExperimental56x = true;
                changed = true;
            }
            ImGui::EndDisabled();
            if (!supported) MenuHelp("Unavailable: the runtime has not reported support for this multiplier.",
                                    "Indisponível: o runtime não informou suporte a este multiplicador.");
        }
        ImGui::EndCombo();
    }
    return changed;
}

void DrawRuntimeOverlayAdvanced(RuntimeOverlay& overlay) {
    if (ImGui::CollapsingHeader(MenuLabel("Neural rendering", "Filtro neural"), ImGuiTreeNodeFlags_DefaultOpen)) DrawNeuralOptions(overlay);
    if (ImGui::CollapsingHeader(MenuLabel("Frame generation", "Geração de quadros"), ImGuiTreeNodeFlags_DefaultOpen)) DrawGenerationOptions(overlay);
    if (ImGui::CollapsingHeader(MenuLabel("Current execution", "Execução atual"))) {
        const auto generation = StreamlineDlssgHook::Instance().Status();
        const auto status = overlay.MenuDrawing().StatusSnapshot();
        ImGui::Text(MenuLabel("NR: %.0f%% | %u pass(es) | GPU %.3f ms", "NR: %.0f%% | %u passagem(ns) | GPU %.3f ms"),
                    status.workingScale * 100, status.passes, status.nrGpuMs);
        ImGui::TextUnformatted(status.beforeUpscale ? MenuLabel("Before DLSS", "Antes do DLSS") : MenuLabel("After DLSS", "Depois do DLSS"));
        ImGui::Text(MenuLabel("Rendered cadence: %.1f FPS", "Cadência renderizada: %.1f FPS"), DlssgTransfusion::Instance().RenderedFps());
        ImGui::Text(MenuLabel("Latest SDK sample: %u presented frames", "Última amostra SDK: %u quadros apresentados"), generation.framesPresentedInSample);
    }
}
} // namespace nrfusion
