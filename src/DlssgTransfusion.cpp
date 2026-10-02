#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/Logger.hpp"

namespace nrfusion {
namespace {

constexpr std::uint32_t kModuleFound = 1u << 0;
constexpr std::uint32_t kArchGatesPatched = 1u << 1;
constexpr std::uint32_t kAdvertiseGatePatched = 1u << 2;
constexpr std::uint32_t kValidateGatePatched = 1u << 3;
constexpr std::uint32_t kBlackwellTransfusionActive = 1u << 4;
constexpr std::uint32_t kUirPatched = 1u << 5;
constexpr std::uint32_t kQualityFixActive = 1u << 6;

bool HasFlag(std::uint32_t flags, std::uint32_t flag) noexcept
{
    return (flags & flag) != 0;
}

} // namespace

DlssgTransfusion& DlssgTransfusion::Instance()
{
    static DlssgTransfusion instance;
    return instance;
}

DlssgTransfusion::DlssgTransfusion()
{
    m_status.effectiveMultiplier = 2;
    char envBuf[32] = {};
    if (GetEnvironmentVariableA("NRFUSION_MFG_SELECTOR", envBuf, sizeof(envBuf)) > 0)
    {
        if (_stricmp(envBuf, "all") == 0)
        {
            m_status.kernelSelector = MfgKernelSelector::All;
        }
        else if (_stricmp(envBuf, "stock") == 0 || _stricmp(envBuf, "stockonly") == 0)
        {
            m_status.kernelSelector = MfgKernelSelector::StockOnly;
        }
        else
        {
            m_status.kernelSelector = MfgKernelSelector::Selective;
        }
    }
    m_kernelSelector.store(m_status.kernelSelector, std::memory_order_relaxed);
}

void DlssgTransfusion::SetKernelSelector(MfgKernelSelector selector) noexcept
{
    std::lock_guard lock(m_mutex);
    m_status.kernelSelector = selector;
    m_kernelSelector.store(selector, std::memory_order_release);
    const auto currentFailure = static_cast<TransfusionFailure>(
        m_snapshotFailure.load(std::memory_order_relaxed));
    PublishSnapshotLocked(currentFailure);
}

MfgKernelSelector DlssgTransfusion::GetKernelSelector() const noexcept
{
    return m_kernelSelector.load(std::memory_order_acquire);
}

void DlssgTransfusion::ResetForTesting() noexcept
{
    std::lock_guard lock(m_mutex);
    m_status = TransfusionStatus{};
    m_lastPatchedModule = nullptr;
    m_appliedOnce.store(false, std::memory_order_relaxed);
    m_activeMultiplier.store(0, std::memory_order_relaxed);
    m_pendingMultiplier.store(0, std::memory_order_relaxed);
    m_stabilityCount.store(0, std::memory_order_relaxed);
    m_kernelSelector.store(MfgKernelSelector::Selective, std::memory_order_relaxed);
    m_snapshotKernelsKeptStock.store(0, std::memory_order_relaxed);
    PublishSnapshotLocked(TransfusionFailure::None);
}

bool DlssgTransfusion::IsPending() const noexcept
{
    return !Snapshot().moduleFound;
}

uint32_t DlssgTransfusion::UnlockedMaxLocked() const noexcept
{
    return m_status.advertiseGatePatched &&
           m_status.validateGatePatched &&
           (m_status.blackwellKernelsRewritten > 0 || m_status.kernelSelector == MfgKernelSelector::StockOnly) ? 5u : 0u;
}

uint32_t DlssgTransfusion::UnlockedMax() const noexcept
{
    return Snapshot().unlockedMax;
}

TransfusionSnapshot DlssgTransfusion::Snapshot() const noexcept
{
    for (;;)
    {
        const auto begin = m_snapshotSequence.load(std::memory_order_acquire);
        if ((begin & 1u) != 0) continue;
        const auto flags = m_snapshotFlags.load(std::memory_order_relaxed);
        const auto kernels = m_snapshotKernels.load(std::memory_order_relaxed);
        const auto kernelsKeptStock = m_snapshotKernelsKeptStock.load(std::memory_order_relaxed);
        const auto selector = m_kernelSelector.load(std::memory_order_relaxed);
        const auto failure = static_cast<TransfusionFailure>(
            m_snapshotFailure.load(std::memory_order_relaxed));
        if (begin != m_snapshotSequence.load(std::memory_order_acquire))
            continue;

        TransfusionSnapshot snapshot;
        snapshot.moduleFound = HasFlag(flags, kModuleFound);
        snapshot.archGatesPatched = HasFlag(flags, kArchGatesPatched);
        snapshot.advertiseGatePatched = HasFlag(flags, kAdvertiseGatePatched);
        snapshot.validateGatePatched = HasFlag(flags, kValidateGatePatched);
        snapshot.blackwellTransfusionActive =
            HasFlag(flags, kBlackwellTransfusionActive);
        snapshot.uirPatched = HasFlag(flags, kUirPatched);
        snapshot.qualityFixActive = HasFlag(flags, kQualityFixActive);
        snapshot.blackwellKernelsRewritten = kernels;
        snapshot.blackwellKernelsKeptStock = kernelsKeptStock;
        snapshot.kernelSelector = selector;
        snapshot.requestedByGame =
            m_requestedByGame.load(std::memory_order_acquire);
        snapshot.effectiveMultiplier =
            m_effectiveMultiplier.load(std::memory_order_acquire);
        snapshot.unlockedMax =
            snapshot.advertiseGatePatched &&
            snapshot.validateGatePatched &&
            (kernels > 0 || selector == MfgKernelSelector::StockOnly) ? 5u : 0u;
        snapshot.failure = failure;
        return snapshot;
    }
}

void DlssgTransfusion::PublishSnapshotLocked(
    TransfusionFailure failure) noexcept
{
    std::uint32_t flags = 0;
    if (m_status.moduleFound) flags |= kModuleFound;
    if (m_status.archGatesPatched) flags |= kArchGatesPatched;
    if (m_status.advertiseGatePatched) flags |= kAdvertiseGatePatched;
    if (m_status.validateGatePatched) flags |= kValidateGatePatched;
    if (m_status.blackwellTransfusionActive)
        flags |= kBlackwellTransfusionActive;
    if (m_status.uirPatched) flags |= kUirPatched;
    if (m_status.qualityFixActive) flags |= kQualityFixActive;

    m_snapshotSequence.fetch_add(1, std::memory_order_acq_rel);
    m_snapshotFlags.store(flags, std::memory_order_relaxed);
    m_snapshotKernels.store(
        m_status.blackwellKernelsRewritten, std::memory_order_relaxed);
    m_snapshotKernelsKeptStock.store(
        m_status.blackwellKernelsKeptStock, std::memory_order_relaxed);
    m_kernelSelector.store(
        m_status.kernelSelector, std::memory_order_relaxed);
    m_snapshotFailure.store(
        static_cast<std::uint8_t>(failure), std::memory_order_relaxed);
    m_snapshotSequence.fetch_add(1, std::memory_order_release);
}

TransfusionStatus DlssgTransfusion::Status() const
{
    std::lock_guard lock(m_mutex);
    TransfusionStatus snapshot = m_status;
    snapshot.requestedByGame = m_requestedByGame.load(std::memory_order_acquire);
    snapshot.effectiveMultiplier = m_effectiveMultiplier.load(std::memory_order_acquire);
    return snapshot;
}

void DlssgTransfusion::TryApply(HMODULE module)
{
    if (!module)
        module = GetModuleHandleW(L"nvngx_dlssg.dll");

    if (!module)
        return;

    wchar_t modPath[MAX_PATH] = {};
    if (GetModuleFileNameW(module, modPath, MAX_PATH) == 0)
    {
        wcscpy_s(modPath, L"<in-memory module>");
    }

    std::lock_guard lock(m_mutex);
    NRF_LOG_INFO("Transfusion", "TryApply called on module %p (%ls)", module, modPath);

    if (m_lastPatchedModule == module && m_status.moduleFound && m_status.advertiseGatePatched &&
        (m_status.blackwellTransfusionActive || m_status.kernelSelector == MfgKernelSelector::StockOnly))
    {
        NRF_LOG_INFO("Transfusion", "Module %p (%ls) already patched, skipping duplicate apply", module, modPath);
        return;
    }

    m_status.moduleFound = true;
    m_status.failureReason.clear();

    if (!HasSupportedArchGates(module))
    {
        m_status.failureReason = "unsupported DLSSG gate signatures";
        NRF_LOG_WARN("Transfusion", "Unsupported DLSSG gate signatures in %ls", modPath);
        PublishSnapshotLocked(TransfusionFailure::UnsupportedGateSignatures);
        return;
    }

    if (!TransfuseBlackwellFatbins(module))
    {
        m_status.failureReason = "no compatible Blackwell fatbins";
        NRF_LOG_WARN("Transfusion", "No compatible Blackwell fatbins found in %ls", modPath);
        PublishSnapshotLocked(TransfusionFailure::NoCompatibleBlackwellFatbins);
        return;
    }
    NRF_LOG_INFO("Transfusion", "Fatbin transfusion completed! Rewritten: %u, Kept stock: %u",
                 m_status.blackwellKernelsRewritten, m_status.blackwellKernelsKeptStock);

    if (!PatchArchGates(module))
    {
        m_status.failureReason = "DLSSG gate patch failed";
        NRF_LOG_WARN("Transfusion", "DLSSG gate patch failed in %ls", modPath);
        PublishSnapshotLocked(TransfusionFailure::GatePatchFailed);
        return;
    }
    NRF_LOG_INFO("Transfusion", "Arch gates patched successfully! AdvertiseGate=%d, ValidateGate=%d",
                 m_status.advertiseGatePatched, m_status.validateGatePatched);

    if (m_uiMode.load(std::memory_order_acquire) == MfgUiMode::Auto)
        PatchHudlessUi(module);

    m_status.moduleFound = true;
    m_lastPatchedModule = module;
    m_appliedOnce.store(true, std::memory_order_release);
    m_status.failureReason.clear();
    PublishSnapshotLocked(TransfusionFailure::None);
    NRF_LOG_INFO("Transfusion", "DLSS-G Transfusion FULLY APPLIED to %ls! Max frames unlocked: 5", modPath);
}

} // namespace nrfusion
