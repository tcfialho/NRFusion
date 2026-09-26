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

uint32_t DlssgTransfusion::UnlockedMaxLocked() const noexcept
{
    return m_status.advertiseGatePatched &&
           m_status.validateGatePatched &&
           m_status.blackwellKernelsRewritten > 0 ? 5u : 0u;
}

uint32_t DlssgTransfusion::UnlockedMax() const noexcept
{
    std::lock_guard lock(m_mutex);
    return UnlockedMaxLocked();
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

    if (!TransfuseBlackwellFatbins(module))
        return;

    if (!PatchArchGates(module))
        return;

    if (m_uiMode.load(std::memory_order_acquire) == MfgUiMode::Auto)
        PatchHudlessUi(module);
}

} // namespace nrfusion
