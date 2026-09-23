#pragma once

#include "nrfusion/D3D12CarrierContract.hpp"
#include "nrfusion/NrSession.hpp"

#include <optional>

namespace nrfusion {

struct D3D12CarrierFramePacket {
    GameContext game{};
    D3D12AcquireSnapshot acquire{};
    TelemetrySample telemetry{};
    RuntimeCapabilities capabilities{};
    SchedulerMode requestedScheduler = SchedulerMode::Auto;
    unsigned generationMultiplier = 1;
    const CompatibilityOverride* compatibility = nullptr;
};

struct D3D12CarrierWork {
    WorkTicket ticket{};
    std::uint64_t submissionEpoch = 0;

    constexpr explicit operator bool() const noexcept {
        return ticket.id != 0 && ticket.session != 0 &&
               submissionEpoch == ticket.id;
    }
};

struct D3D12CarrierFrameResult {
    D3D12AcquireResult acquire{};
    NrSessionFrameResult session{};

    constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(acquire) && static_cast<bool>(session);
    }
};

class D3D12CarrierSession {
public:
    bool Configure(
        const RuntimeConfig& config, const PerformanceConfig& performance);
    void Reset() noexcept;
    D3D12CarrierFrameResult Resolve(const D3D12CarrierFramePacket& packet);
    std::optional<D3D12CarrierWork> BeginWork(
        const D3D12CarrierFrameResult& frame, std::uint64_t viewKey = 0) noexcept;
    bool CanExecuteWork(const D3D12CarrierWork& work) const noexcept;
    bool ClaimExecuteWork(const D3D12CarrierWork& work) noexcept;
    bool SubmitWork(const D3D12CarrierWork& work) noexcept;
    bool AbandonWork(const D3D12CarrierWork& work) noexcept;
    bool MapTimedWork(const D3D12CarrierWork& work) noexcept;
    bool RetireTimedSample(const NrRetiredTimingSample& sample);
    bool RetireTimedInterval(double gpuMs);

    const RuntimeConfig& Config() const noexcept { return session_.Config(); }
    const NrSessionState& State() const noexcept { return session_.State(); }

private:
    NrSession session_{};
};

} // namespace nrfusion
