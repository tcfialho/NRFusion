#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "nrfusion/DlssgTransfusion.hpp"
#include <algorithm>
#include <cmath>

namespace nrfusion {

void DlssgTransfusion::SetControlMode(MfgControlMode mode) noexcept
{
    if (m_controlMode.exchange(mode, std::memory_order_acq_rel) != mode)
        m_memoryMultiplierLimit.store(6, std::memory_order_release);
}

MfgControlMode DlssgTransfusion::GetControlMode() const noexcept
{
    return m_controlMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetOverrideMultiplier(uint32_t multiplier) noexcept
{
    m_overrideMultiplier.store(multiplier, std::memory_order_release);
}

void DlssgTransfusion::ForceMultiplier(uint32_t multiplier) noexcept
{
    const uint32_t frames = (multiplier > 1) ? (multiplier - 1) : 0;
    m_overrideMultiplier.store(multiplier, std::memory_order_release);
    m_activeMultiplier.store(frames, std::memory_order_release);
    m_pendingMultiplier.store(frames, std::memory_order_release);
    m_stabilityCount.store(8, std::memory_order_release);
    m_appliedOnce.store(true, std::memory_order_release);
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

void DlssgTransfusion::ObserveRenderedFrame() noexcept {
    static const double frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const auto previous = m_lastRenderCounter.exchange(static_cast<std::uint64_t>(counter.QuadPart));
    m_renderedFrames.fetch_add(1);
    if (!previous || frequency <= 0) return;
    const double elapsed = (counter.QuadPart - previous) / frequency;
    if (elapsed < 0.001 || elapsed > 0.25) return;
    const float sample = static_cast<float>(1.0 / elapsed);
    const float prior = m_renderedFps.load();
    m_renderedFps.store(prior > 0 ? prior + 0.10f * (sample - prior) : sample);
}

float DlssgTransfusion::RenderedFps() const noexcept { return m_renderedFps.load(); }

void DlssgTransfusion::ObserveDisplayRefresh(uint32_t hz) noexcept {
    if (hz > 1 && hz < 1000) m_displayRefreshHz.store(hz);
}

void DlssgTransfusion::ObserveAcceptedOptions(uint32_t mode, uint32_t generatedFrames) noexcept {
    m_effectiveMultiplier.store(mode == 0 ? 1u : generatedFrames + 1u);
}

void DlssgTransfusion::ObserveGenerationLimit(uint32_t frames) noexcept { m_reportedGenerationLimit.store(frames); }
void DlssgTransfusion::SetAutomaticMultiplierLimit(uint32_t multiplier) noexcept { m_automaticMultiplierLimit.store(std::clamp(multiplier, 2u, 6u)); }
uint32_t DlssgTransfusion::AutomaticMultiplierLimit() const noexcept {
    return std::min(m_automaticMultiplierLimit.load(), m_memoryMultiplierLimit.load());
}

bool DlssgTransfusion::ObserveVramWarning(uint32_t generatedFrames) noexcept {
    if (!m_respectVramBudget.load() || GetControlMode() != MfgControlMode::Dynamic || generatedFrames <= 1) return false;
    const auto limit = std::clamp(generatedFrames, 2u, 6u);
    auto prior = m_memoryMultiplierLimit.load();
    while (limit < prior) {
        if (m_memoryMultiplierLimit.compare_exchange_weak(prior, limit)) return true;
    }
    return false;
}
void DlssgTransfusion::SetRespectVramBudget(bool enabled) noexcept {
    m_respectVramBudget.store(enabled);
    if (!enabled) m_memoryMultiplierLimit.store(6);
}
bool DlssgTransfusion::TransitionPending() const noexcept { return m_pendingMultiplier.load() != m_activeMultiplier.load(); }

void DlssgTransfusion::ProcessSetOptions(uint32_t& inOutMode, uint32_t& inOutNumFramesToGenerate)
{
    m_requestedByGame.store(inOutNumFramesToGenerate, std::memory_order_release);

    uint32_t targetFrames = inOutNumFramesToGenerate;
    const auto control = m_controlMode.load(std::memory_order_acquire);

    if (control == MfgControlMode::FollowGame)
    {
        m_activeMultiplier.store(inOutNumFramesToGenerate);
        m_pendingMultiplier.store(inOutNumFramesToGenerate);
        m_appliedOnce.store(inOutMode != 0);
        return;
    }
    else if (control == MfgControlMode::OverrideFixed)
    {
        const uint32_t overrideVal = m_overrideMultiplier.load(std::memory_order_acquire);
        if (overrideVal > 1)
        {
            inOutMode = 1; // sl::DLSSGMode::eOn
            targetFrames = overrideVal - 1;
        }
        else
        {
            inOutMode = 0; // sl::DLSSGMode::eOff
            targetFrames = 0;
        }
    }
    else if (control == MfgControlMode::Dynamic)
    {
        const float sourceFps = m_renderedFps.load();
        const uint32_t target = m_dynamicTargetFps.load() ? m_dynamicTargetFps.load() : m_displayRefreshHz.load();
        const uint32_t available = std::max(UnlockedMax(), m_reportedGenerationLimit.load());
        const uint32_t limit = std::max(2u, std::min(AutomaticMultiplierLimit(), available + 1u));
        uint32_t desired = std::clamp(m_dynamicMultiplier.load(), 2u, limit);
        if (sourceFps > 1) {
            const float needed = target / sourceFps;
            if (needed > desired + 0.15f || needed < desired - 1.15f)
                desired = std::clamp(static_cast<uint32_t>(std::ceil(needed)), 2u, limit);
        }
        m_dynamicMultiplier.store(desired);
        inOutMode = 1;
        targetFrames = desired - 1;
    }

    if (inOutMode == 0) {
        inOutNumFramesToGenerate = 0;
        m_activeMultiplier.store(0);
        m_pendingMultiplier.store(0);
        m_appliedOnce.store(false);
        return;
    }
    const uint32_t available = std::max(UnlockedMax(), m_reportedGenerationLimit.load());
    if (available) targetFrames = std::min(targetFrames, available);

    // Trava de Transição Segura Anti-TDR:
    // Evita reconstrução destrutiva de heaps D3D12 caso o multiplicador seja alternado rapidamente
    if (!m_appliedOnce.load(std::memory_order_acquire))
    {
        m_activeMultiplier.store(targetFrames, std::memory_order_release);
        m_pendingMultiplier.store(targetFrames, std::memory_order_release);
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
            if (m_renderedFrames.load() - m_pendingStartFrame.load() >= 8)
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
            m_pendingStartFrame.store(m_renderedFrames.load());
            m_stabilityCount.store(1, std::memory_order_release);
            inOutNumFramesToGenerate = m_activeMultiplier.load(std::memory_order_acquire);
        }
    }

}

void DlssgTransfusion::ProcessGetState(uint32_t& outNumFramesToGenerateMax)
{
    const uint32_t unlockedMax = UnlockedMax();
    if (unlockedMax != 0 && outNumFramesToGenerateMax < unlockedMax)
        outNumFramesToGenerateMax = unlockedMax;
}

void DlssgTransfusion::NotifyFrameBoundary()
{
    // Limite seguro de Present
}

} // namespace nrfusion
