#pragma once

#include "nrfusion/NrSession.hpp"
#include "nrfusion/VulkanCarrierAcquire.hpp"

#include <optional>

namespace nrfusion {

struct VulkanCarrierFramePacket {
    GameContext game{};
    VulkanNativeAcquireInput acquire{};
    TelemetrySample telemetry{};
    RuntimeCapabilities capabilities{};
    SchedulerMode requestedScheduler = SchedulerMode::Auto;
    unsigned generationMultiplier = 1;
    const CompatibilityOverride* compatibility = nullptr;
};

struct VulkanCarrierWork {
    WorkTicket ticket{};
    std::uint64_t submissionEpoch = 0;

    constexpr explicit operator bool() const noexcept {
        return ticket.id != 0 && ticket.session != 0 &&
               submissionEpoch == ticket.id;
    }
};

struct VulkanCarrierFrameResult {
    VulkanAcquireResult acquire{};
    NrSessionFrameResult session{};

    constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(acquire) &&
               static_cast<bool>(session);
    }
};

class VulkanCarrierSession {
public:
    bool Configure(
        const RuntimeConfig& config,
        const PerformanceConfig& performance);
    void Reset() noexcept;

    VulkanCarrierFrameResult Resolve(
        const VulkanCarrierFramePacket& packet);
    std::optional<VulkanCarrierWork> BeginWork(
        const VulkanCarrierFrameResult& frame,
        std::uint64_t viewKey = 0) noexcept;

    bool CanExecuteWork(const VulkanCarrierWork& work) const noexcept;
    bool ClaimExecuteWork(const VulkanCarrierWork& work) noexcept;
    bool HasClaimedExecution(const VulkanCarrierWork& work) const noexcept;
    bool ConsumeClaimedExecution(const VulkanCarrierWork& work) noexcept;
    bool SubmitWork(const VulkanCarrierWork& work) noexcept;
    bool AbandonWork(const VulkanCarrierWork& work) noexcept;
    bool MapTimedWork(const VulkanCarrierWork& work) noexcept;
    bool RetireTimedSample(const NrRetiredTimingSample& sample);
    bool RetireTimedInterval(double gpuMs);

    const RuntimeConfig& Config() const noexcept {
        return session_.Config();
    }
    const NrSessionState& State() const noexcept {
        return session_.State();
    }

private:
    NrSession session_{};
};

} // namespace nrfusion
