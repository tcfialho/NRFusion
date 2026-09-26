#include "nrfusion/DlssgTransfusion.hpp"

namespace nrfusion {

void DlssgTransfusion::SetControlMode(MfgControlMode mode) noexcept
{
    m_controlMode.store(mode, std::memory_order_release);
}

MfgControlMode DlssgTransfusion::GetControlMode() const noexcept
{
    return m_controlMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetOverrideMultiplier(uint32_t multiplier) noexcept
{
    m_overrideMultiplier.store(multiplier, std::memory_order_release);
}

uint32_t DlssgTransfusion::GetOverrideMultiplier() const noexcept
{
    return m_overrideMultiplier.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetQualityMode(MfgQualityMode mode) noexcept
{
    m_qualityMode.store(mode, std::memory_order_release);
}

MfgQualityMode DlssgTransfusion::GetQualityMode() const noexcept
{
    return m_qualityMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetUiMode(MfgUiMode mode) noexcept
{
    m_uiMode.store(mode, std::memory_order_release);
}

MfgUiMode DlssgTransfusion::GetUiMode() const noexcept
{
    return m_uiMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetMotionVectorMode(MfgMotionVectorMode mode) noexcept
{
    m_mvMode.store(mode, std::memory_order_release);
}

MfgMotionVectorMode DlssgTransfusion::GetMotionVectorMode() const noexcept
{
    return m_mvMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetDynamicTargetFps(uint32_t fps) noexcept
{
    m_dynamicTargetFps.store(fps, std::memory_order_release);
}

uint32_t DlssgTransfusion::GetDynamicTargetFps() const noexcept
{
    return m_dynamicTargetFps.load(std::memory_order_acquire);
}

void DlssgTransfusion::ProcessSetOptions(uint32_t& inOutMode, uint32_t& inOutNumFramesToGenerate)
{
    std::lock_guard lock(m_mutex);
    m_status.requestedByGame = inOutNumFramesToGenerate;

    uint32_t targetFrames = inOutNumFramesToGenerate;
    const auto control = m_controlMode.load(std::memory_order_acquire);

    if (control == MfgControlMode::FollowGame)
    {
        // FollowGame: respeita exatamente a solicitação do jogo
        targetFrames = inOutNumFramesToGenerate;
    }
    else if (control == MfgControlMode::OverrideFixed)
    {
        const uint32_t overrideVal = m_overrideMultiplier.load(std::memory_order_acquire);
        targetFrames = (overrideVal > 1) ? (overrideVal - 1) : 0;
    }
    else if (control == MfgControlMode::Dynamic)
    {
        inOutMode = 2; // sl::DLSSGMode::eDynamic
    }

    // Trava de Transição Segura Anti-TDR:
    // Evita reconstrução destrutiva de heaps D3D12 caso o multiplicador seja alternado rapidamente
    if (!m_appliedOnce.load(std::memory_order_acquire))
    {
        m_activeMultiplier.store(targetFrames, std::memory_order_release);
        m_appliedOnce.store(true, std::memory_order_release);
        inOutNumFramesToGenerate = targetFrames;
    }
    else if (targetFrames == m_activeMultiplier.load(std::memory_order_acquire))
    {
        m_pendingMultiplier.store(targetFrames, std::memory_order_release);
        m_stabilityCount.store(0, std::memory_order_release);
        inOutNumFramesToGenerate = targetFrames;
    }
    else
    {
        // Alvo mudou: aplica janela de estabilização de 8 frames (~100-150ms)
        if (m_pendingMultiplier.load(std::memory_order_acquire) == targetFrames)
        {
            uint32_t count = m_stabilityCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (count >= 8)
            {
                m_activeMultiplier.store(targetFrames, std::memory_order_release);
                inOutNumFramesToGenerate = targetFrames;
            }
            else
            {
                inOutNumFramesToGenerate = m_activeMultiplier.load(std::memory_order_acquire);
            }
        }
        else
        {
            m_pendingMultiplier.store(targetFrames, std::memory_order_release);
            m_stabilityCount.store(1, std::memory_order_release);
            inOutNumFramesToGenerate = m_activeMultiplier.load(std::memory_order_acquire);
        }
    }

    m_status.effectiveMultiplier = inOutNumFramesToGenerate + 1;
}

void DlssgTransfusion::ProcessGetState(uint32_t& outNumFramesToGenerateMax)
{
    std::lock_guard lock(m_mutex);
    if (outNumFramesToGenerateMax < 5)
        outNumFramesToGenerateMax = 5; // Suporta até 6X na API
}

void DlssgTransfusion::NotifyFrameBoundary()
{
    // Limite seguro de Present
}

} // namespace nrfusion
