#include "nrfusion/DlssgTransfusion.hpp"

namespace nrfusion {

DlssgTransfusion& DlssgTransfusion::Instance()
{
    static DlssgTransfusion instance;
    return instance;
}

DlssgTransfusion::DlssgTransfusion()
{
    m_status.effectiveMultiplier = 2;
}

bool DlssgTransfusion::IsPending() const noexcept
{
    std::lock_guard lock(m_mutex);
    return !m_status.moduleFound;
}

uint32_t DlssgTransfusion::UnlockedMax() const noexcept
{
    return 5; // Suporte até 6X (5 gerados)
}

TransfusionStatus DlssgTransfusion::Status() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

void DlssgTransfusion::TryApply(HMODULE module)
{
    std::lock_guard lock(m_mutex);
    if (m_status.moduleFound)
        return;

    if (!module)
        module = GetModuleHandleW(L"nvngx_dlssg.dll");

    if (!module)
        return;

    m_status.moduleFound = true;

    // 1. Patchear checagens de arquitetura Ada (0x1b0 -> 0x190)
    PatchArchGates(module);

    // 2. HUDless UI Recomposition
    if (m_uiMode.load(std::memory_order_acquire) == MfgUiMode::Auto)
    {
        PatchHudlessUi(module);
    }

    // 3. Transfusão de Kernels Blackwell (sm_120 -> sm_89)
    TransfuseBlackwellFatbins(module);
}

} // namespace nrfusion
