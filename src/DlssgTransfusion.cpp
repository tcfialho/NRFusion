#include "nrfusion/DlssgTransfusion.hpp"

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
}

bool DlssgTransfusion::IsPending() const noexcept
{
    return !Snapshot().moduleFound;
}

uint32_t DlssgTransfusion::UnlockedMaxLocked() const noexcept
{
    return m_status.advertiseGatePatched &&
           m_status.validateGatePatched &&
           m_status.blackwellKernelsRewritten > 0 ? 5u : 0u;
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
        snapshot.requestedByGame =
            m_requestedByGame.load(std::memory_order_acquire);
        snapshot.effectiveMultiplier =
            m_effectiveMultiplier.load(std::memory_order_acquire);
        snapshot.unlockedMax =
            snapshot.advertiseGatePatched &&
            snapshot.validateGatePatched && kernels > 0 ? 5u : 0u;
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

    std::lock_guard lock(m_mutex);
    if (m_lastPatchedModule == module && m_status.moduleFound && m_status.advertiseGatePatched && m_status.blackwellTransfusionActive)
        return;

    if (!HasSupportedArchGates(module))
    {
        if (!m_status.moduleFound)
        {
            m_status.failureReason = "unsupported DLSSG gate signatures";
            PublishSnapshotLocked(TransfusionFailure::UnsupportedGateSignatures);
        }
        return;
    }

    if (!TransfuseBlackwellFatbins(module))
    {
        m_status.failureReason = "no compatible Blackwell fatbins";
        PublishSnapshotLocked(TransfusionFailure::NoCompatibleBlackwellFatbins);
        return;
    }

    if (!PatchArchGates(module))
    {
        m_status.failureReason = "DLSSG gate patch failed";
        PublishSnapshotLocked(TransfusionFailure::GatePatchFailed);
        return;
    }

    if (m_uiMode.load(std::memory_order_acquire) == MfgUiMode::Auto)
        PatchHudlessUi(module);

    m_status.moduleFound = true;
    m_lastPatchedModule = module;
    m_appliedOnce.store(true, std::memory_order_release);
    m_status.failureReason.clear();
    PublishSnapshotLocked(TransfusionFailure::None);
}

} // namespace nrfusion
