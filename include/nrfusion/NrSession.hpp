#pragma once

#include "nrfusion/FusionRuntime.hpp"
#include "nrfusion/NrSessionContracts.hpp"
#include "nrfusion/NrSessionWorkState.hpp"
#include "nrfusion/NrTimingSource.hpp"

#include <optional>

namespace nrfusion {

class NrSession {
public:
    NrSession() = default;

    bool Configure(const RuntimeConfig& config, const PerformanceConfig& performance);
    void Reset() noexcept;
    NrSessionFrameResult Resolve(const NrSessionFramePacket& packet);

    std::optional<WorkTicket> BeginWork(
        const NrSessionFrameResult& frame, std::uint64_t viewKey = 0) noexcept;
    bool CanExecuteWork(const WorkTicket& ticket) const noexcept;
    bool ClaimExecuteWork(const WorkTicket& ticket) noexcept;
    bool SubmitWork(const WorkTicket& ticket) noexcept;
    bool AbandonWork(const WorkTicket& ticket) noexcept;
    bool MapTimedWork(const WorkTicket& ticket) noexcept;
    void MapInvalidTimedAttempt() noexcept;
    bool RetireTimedSample(const NrRetiredTimingSample& sample);
    bool RetireTimedInterval(double gpuMs);

    const RuntimeConfig& Config() const noexcept { return config_; }
    const NrSessionState& State() const noexcept { return state_; }

private:
    NrSessionFrameResult Reject(
        const NrSessionFramePacket& packet, NrSessionDisposition disposition) noexcept;

    RuntimeConfig config_{};
    PerformanceConfig performanceConfig_{};
    NrSessionState state_{};
    FusionRuntime runtime_{};
    NrSessionWorkTracker works_{};
    NrSessionTimingQueue timings_{};
    std::optional<double> retiredNrGpuMs_;
    bool configured_ = false;
};

} // namespace nrfusion
