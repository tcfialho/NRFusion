#pragma once

#include "nrfusion/FusionRuntime.hpp"
#include "nrfusion/NrSessionContracts.hpp"

namespace nrfusion {

class NrSession {
public:
    NrSession() = default;

    bool Configure(const RuntimeConfig& config, const PerformanceConfig& performance);
    void Reset() noexcept;
    NrSessionFrameResult Resolve(const NrSessionFramePacket& packet);

    const RuntimeConfig& Config() const noexcept { return config_; }
    const NrSessionState& State() const noexcept { return state_; }

private:
    NrSessionFrameResult Reject(
        const NrSessionFramePacket& packet, NrSessionDisposition disposition) noexcept;

    RuntimeConfig config_{};
    NrSessionState state_{};
    FusionRuntime runtime_{};
    bool configured_ = false;
};

} // namespace nrfusion
