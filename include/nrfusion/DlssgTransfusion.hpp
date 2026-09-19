#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <windows.h>

namespace nrfusion {

enum class MfgControlMode : uint32_t {
    FollowGame = 0,    // Respeita o menu nativo do jogo (2X, 3X, 4X) sem forçar override
    OverrideFixed = 1, // Força multiplicador fixo configurado no OptiScaler
    Dynamic = 2        // Dynamic Frame Generation baseado na taxa de atualização
};

enum class MfgQualityMode : uint32_t {
    Performance = 0,   // Stock scatter rejection, menor latência, zero overhead de GPU (default)
    Enhanced = 1       // Injeção PTX qualityValidWarp no BlendCandidatesFused para grades/fios
};

enum class MfgUiMode : uint32_t {
    Auto = 0,          // Auto-detecta buffers HUDless e ativa UIR (default)
    Off = 1            // Desativa HUDless UI Recomposition
};

enum class MfgMotionVectorMode : uint32_t {
    Auto = 0,          // Dilatação padrão de motion vectors (default)
    DisableDilation = 1 // Troubleshooting: desativa dilatação de MV
};

struct TransfusionStatus {
    bool moduleFound = false;
    bool archGatesPatched = false;
    unsigned int archGatesCount = 0;
    bool blackwellTransfusionActive = false;
    unsigned int blackwellKernelsRewritten = 0;
    bool uirPatched = false;
    bool qualityFixActive = false;
    uint32_t requestedByGame = 0;     // 0 = off, 1 = 2X, 2 = 3X, 3 = 4X
    uint32_t effectiveMultiplier = 2; // Multiplicador efetivo final
    std::string snippetVersion;
    std::string failureReason;
};

class DlssgTransfusion {
public:
    static DlssgTransfusion& Instance();

    // Configuração
    void SetControlMode(MfgControlMode mode) noexcept;
    MfgControlMode GetControlMode() const noexcept;

    void SetOverrideMultiplier(uint32_t multiplier) noexcept;
    uint32_t GetOverrideMultiplier() const noexcept;

    void SetQualityMode(MfgQualityMode mode) noexcept;
    MfgQualityMode GetQualityMode() const noexcept;

    void SetUiMode(MfgUiMode mode) noexcept;
    MfgUiMode GetUiMode() const noexcept;

    void SetMotionVectorMode(MfgMotionVectorMode mode) noexcept;
    MfgMotionVectorMode GetMotionVectorMode() const noexcept;

    void SetDynamicTargetFps(uint32_t fps) noexcept;
    uint32_t GetDynamicTargetFps() const noexcept;

    // Patches aplicados no carregamento de nvngx_dlssg.dll
    void TryApply(HMODULE module = nullptr);
    bool IsPending() const noexcept;
    uint32_t UnlockedMax() const noexcept;

    // Interceptação Streamline (slDLSSGSetOptions) com Safe Transition anti-TDR
    void ProcessSetOptions(uint32_t& inOutMode, uint32_t& inOutNumFramesToGenerate);

    // Interceptação Streamline (slDLSSGGetState)
    void ProcessGetState(uint32_t& outNumFramesToGenerateMax);

    // Sincronização na fronteira de frame / Present
    void NotifyFrameBoundary();

    TransfusionStatus Status() const;

private:
    DlssgTransfusion();
    ~DlssgTransfusion() = default;

    bool PatchArchGates(HMODULE module);
    bool PatchHudlessUi(HMODULE module);
    bool TransfuseBlackwellFatbins(HMODULE module);

    mutable std::mutex m_mutex;
    TransfusionStatus m_status;

    std::atomic<MfgControlMode> m_controlMode{MfgControlMode::FollowGame};
    std::atomic<uint32_t> m_overrideMultiplier{2};
    std::atomic<MfgQualityMode> m_qualityMode{MfgQualityMode::Performance};
    std::atomic<MfgUiMode> m_uiMode{MfgUiMode::Auto};
    std::atomic<MfgMotionVectorMode> m_mvMode{MfgMotionVectorMode::Auto};
    std::atomic<uint32_t> m_dynamicTargetFps{0};

    // Safe Transition anti-TDR
    std::atomic<uint32_t> m_activeMultiplier{0};
    std::atomic<uint32_t> m_pendingMultiplier{0};
    std::atomic<uint32_t> m_stabilityCount{0};
    std::atomic<bool> m_appliedOnce{false};
};

} // namespace nrfusion
