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
    changed |= ImGui::Checkbox(MenuLabel("Use monitor refresh as Dynamic target", "Usar frequência do monitor como meta do Dinâmico"), &main.displayHzAuto);
    changed |= ImGui::Checkbox(MenuLabel("Enable 5X/6X in Dynamic", "Habilitar 5X/6X no Dinâmico"), &advanced.mfg.allowExperimental56x);
    changed |= ImGui::Checkbox(MenuLabel("Reduce Dynamic multiplier when VRAM is insufficient", "Reduzir multiplicador Dinâmico quando faltar VRAM"), &advanced.mfg.respectVramBudget);

    auto currentLatencyMode = StreamlineDlssgHook::Instance().GetMfgLatencyMode();
    int selectedLatency = (currentLatencyMode == MfgLatencyMode::LowLatency) ? 1 : 0;
    const char* latencyLabels[] = {
        MenuLabel("Game Default", "Padrão do Jogo"),
        MenuLabel("Low Latency", "Baixa Latência")
    };
    if (ImGui::BeginCombo(MenuLabel("MFG Latency Mode", "Modo de Latência MFG"), latencyLabels[selectedLatency])) {
        for (int i = 0; i < 2; ++i) {
            if (ImGui::Selectable(latencyLabels[i], i == selectedLatency)) {
                currentLatencyMode = (i == 1) ? MfgLatencyMode::LowLatency : MfgLatencyMode::GameDefault;
                const auto targetHz = StreamlineDlssgHook::Instance().GetTargetDisplayFps();
                StreamlineDlssgHook::Instance().SetMfgLatencyMode(currentLatencyMode, targetHz > 0 ? targetHz : 60);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    MenuHelp("Game Default preserves game Reflex options. Low Latency applies target display FPS via Reflex frameLimitUs.",
             "Game Default preserva opções Reflex do jogo. Low Latency aplica meta de FPS de exibição via Reflex frameLimitUs.");

    if (currentLatencyMode == MfgLatencyMode::LowLatency) {
        uint32_t currentTargetFps = StreamlineDlssgHook::Instance().GetTargetDisplayFps();
        if (currentTargetFps == 0) currentTargetFps = 60;
        int presetIdx = 4;
        if (currentTargetFps == 60) presetIdx = 0;
        else if (currentTargetFps == 90) presetIdx = 1;
        else if (currentTargetFps == 120) presetIdx = 2;
        else if (currentTargetFps == 144) presetIdx = 3;

        const char* targetFpsLabels[] = {"60 FPS", "90 FPS", "120 FPS", "144 FPS", MenuLabel("Custom", "Personalizado")};
        if (ImGui::BeginCombo(MenuLabel("Target Display FPS", "Meta de FPS de Exibição"), targetFpsLabels[presetIdx])) {
            const uint32_t presets[] = {60, 90, 120, 144};
            for (int i = 0; i < 4; ++i) {
                if (ImGui::Selectable(targetFpsLabels[i], i == presetIdx)) {
                    StreamlineDlssgHook::Instance().SetMfgLatencyMode(currentLatencyMode, presets[i]);
                    main.displayHz = static_cast<float>(presets[i]);
                    changed = true;
                }
            }
            if (ImGui::Selectable(targetFpsLabels[4], presetIdx == 4)) {
                // Keep custom
            }
            ImGui::EndCombo();
        }
        MenuHelp("Final displayed FPS cadence. User manual selection always overrides monitor auto-detection.",
                 "Cadência final de FPS de exibição. Seleção manual do usuário sempre tem prioridade sobre detecção do monitor.");

        if (presetIdx == 4) {
            int customFps = static_cast<int>(currentTargetFps);
            if (ImGui::SliderInt(MenuLabel("Custom Target FPS", "FPS Personalizado"), &customFps, 30, 360, "%d FPS")) {
                StreamlineDlssgHook::Instance().SetMfgLatencyMode(currentLatencyMode, static_cast<uint32_t>(customFps));
                main.displayHz = static_cast<float>(customFps);
                changed = true;
            }
        }
    }

    auto currentPacer = StreamlineDlssgHook::Instance().GetMfgPacerMode();
    int selectedPacer = static_cast<int>(currentPacer);
    const char* pacerLabels[] = {
        MenuLabel("Auto", "Automático"),
        MenuLabel("CPU Pacer", "Pacer por CPU"),
        MenuLabel("Flip Metering", "Flip Metering")
    };
    if (ImGui::BeginCombo(MenuLabel("MFG Pacer", "Pacer MFG"), pacerLabels[selectedPacer])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(pacerLabels[i], i == selectedPacer)) {
                currentPacer = static_cast<MfgPacerMode>(i);
                StreamlineDlssgHook::Instance().SetMfgPacerMode(currentPacer);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    MenuHelp("Auto uses NVIDIA default flip metering. CPU Pacer enables scoped CPU presentation pacing for sl.dlss_g on Ada GPUs.",
             "Auto usa medição de flip padrão da NVIDIA. CPU Pacer ativa pacing de apresentação por CPU para sl.dlss_g em GPUs Ada.");

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

        const auto reflex = StreamlineDlssgHook::Instance().ReflexOwnership();
        if (reflex.haveGameOptions) {
            const char* modeName = reflex.latencyMode == 1 ? "LowLatency" : "GameDefault";
            const auto pacerMode = StreamlineDlssgHook::Instance().GetMfgPacerMode();
            const char* pacerName = pacerMode == MfgPacerMode::CpuPacer ? "CpuPacer" :
                                    (pacerMode == MfgPacerMode::FlipMetering ? "FlipMetering" : "Auto");
            ImGui::Text(MenuLabel("Reflex pacing: %s (%s) | target %u Hz | limit %u us",
                                  "Pacing Reflex: %s (%s) | meta %u Hz | limite %u us"),
                        modeName, pacerName, reflex.targetDisplayFps, reflex.effectiveFrameLimitUs);
        }
    }
}
} // namespace nrfusion
