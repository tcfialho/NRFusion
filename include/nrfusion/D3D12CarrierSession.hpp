#pragma once

#include "nrfusion/D3D12CarrierContract.hpp"
#include "nrfusion/NrSession.hpp"

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

    const RuntimeConfig& Config() const noexcept { return session_.Config(); }
    const NrSessionState& State() const noexcept { return session_.State(); }

private:
    NrSession session_{};
};

} // namespace nrfusion
