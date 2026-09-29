#include "nrfusion/RuntimeLocalization.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>

namespace nrfusion {

namespace {

constexpr UiStrings kEnglishStrings = {
    L"NRFUSION CONTROL PANEL",
    L"Universal Standalone",
    L"Main",
    L"Advanced",
    L"[X] Enabled",
    L"[  ] Enabled",
    L"Neural Rendering Mode:",
    L"Auto",
    L"Quality",
    L"Perf",
    L"Custom",
    L"Target Rendered FPS: ",
    L"Display Refresh Rate: ",
    L"Frame Generation (MFG):",
    L"Follow Game",
    L"Fixed",
    L"Dynamic",
    L"Status Telemetry",
    L"NR: Active",
    L"NR: Inactive",
    L"Output: ",
    L"Apply & Save Config",
    L"Neural Precision:",
    L"Pipeline Placement:",
    L"Residual",
    L"Multipass",
    L"MFG 5X/6X",
    L"Diagnostics",
    L"PT",
    "NRFusion Standalone Active (F8 for Menu)",
    "Configuration saved and applied!",
    "Failed to apply runtime reconfiguration",
};

constexpr UiStrings kPortugueseStrings = {
    L"PAINEL DE CONTROLE NRFUSION",
    L"Universal Standalone",
    L"Principal",
    L"Avançado",
    L"[X] Ativado",
    L"[  ] Ativado",
    L"Modo Neural Rendering:",
    L"Automático",
    L"Qualidade",
    L"Desempenho",
    L"Custom",
    L"FPS Renderizado Alvo: ",
    L"Taxa de Atualização da Tela: ",
    L"Geração de Quadros (MFG):",
    L"Seguir Jogo",
    L"Fixo",
    L"Dinâmico",
    L"Telemetria de Status",
    L"NR: Ativo",
    L"NR: Inativo",
    L"Saída: ",
    L"Aplicar e Salvar Config",
    L"Precisão Neural:",
    L"Posicionamento no Pipeline:",
    L"Residual",
    L"Multipass",
    L"MFG 5X/6X",
    L"Diagnósticos",
    L"EN",
    "NRFusion Standalone Ativo (F8 para Menu)",
    "Configurações salvas e aplicadas!",
    "Falha ao aplicar reconfiguração do runtime",
};

std::atomic<UiLanguage> s_currentLanguage{UiLanguage::English};
std::atomic<bool> s_languageInitialized{false};

void EnsureInitialized() noexcept {
    if (!s_languageInitialized.exchange(true)) {
        s_currentLanguage.store(RuntimeLocalization::DetectSystemLanguage());
    }
}

} // namespace

UiLanguage RuntimeLocalization::DetectSystemLanguage() noexcept {
#if defined(_WIN32)
    const LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_PORTUGUESE) {
        return UiLanguage::Portuguese;
    }
#endif
    return UiLanguage::English;
}

void RuntimeLocalization::SetLanguage(UiLanguage lang) noexcept {
    s_languageInitialized.store(true);
    s_currentLanguage.store(lang);
}

UiLanguage RuntimeLocalization::GetLanguage() noexcept {
    EnsureInitialized();
    return s_currentLanguage.load();
}

void RuntimeLocalization::ToggleLanguage() noexcept {
    EnsureInitialized();
    const auto current = s_currentLanguage.load();
    s_currentLanguage.store(current == UiLanguage::English ? UiLanguage::Portuguese : UiLanguage::English);
}

const UiStrings& RuntimeLocalization::Strings() noexcept {
    EnsureInitialized();
    return (s_currentLanguage.load() == UiLanguage::Portuguese) ? kPortugueseStrings : kEnglishStrings;
}

} // namespace nrfusion
