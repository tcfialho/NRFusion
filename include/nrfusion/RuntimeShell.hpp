#pragma once

#include "nrfusion/RuntimeComponentRegistry.hpp"
#include "nrfusion/RuntimeConfig.hpp"

#include <cstdint>

namespace nrfusion {

enum class RuntimeState : std::uint8_t {
    Stopped,
    Running,
    Disabled,
    Failed
};

enum class RuntimeFailure : std::uint8_t {
    None,
    InvalidConfig
};

struct RuntimeStatus {
    RuntimeState state = RuntimeState::Stopped;
    RuntimeFailure failure = RuntimeFailure::None;
    std::uint64_t configGeneration = 0;
};

class RuntimeShell {
public:
    bool Initialize(RuntimeConfig config) noexcept;
    void Shutdown() noexcept;
    bool Reconfigure(RuntimeConfig config) noexcept;

    RuntimeStatus Status() const noexcept { return status_; }
    const RuntimeConfig& Config() const noexcept { return config_; }
    RuntimeComponentRegistry& Registry() noexcept { return registry_; }
    const RuntimeComponentRegistry& Registry() const noexcept { return registry_; }

private:
    RuntimeConfig config_{};
    RuntimeStatus status_{};
    RuntimeComponentRegistry registry_{};
};

} // namespace nrfusion
