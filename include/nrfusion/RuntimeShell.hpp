#pragma once

#include "nrfusion/RuntimeComponentRegistry.hpp"
#include "nrfusion/RuntimeConfig.hpp"

#include <atomic>
#include <cstdint>

namespace nrfusion {

class RuntimeBootstrap;

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

static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

class RuntimeShell {
public:
    bool Initialize(RuntimeConfig config) noexcept;
    void Shutdown() noexcept;
    bool Reconfigure(RuntimeConfig config) noexcept;

    RuntimeStatus Status() const noexcept;
    RuntimeConfig Config() const noexcept { return config_; }
    const RuntimeComponentRegistry& Registry() const noexcept { return registry_; }

private:
    friend class RuntimeBootstrap;
    void PublishStatus(RuntimeStatus status) noexcept;

    RuntimeConfig config_{};
    std::atomic<std::uint64_t> statusSequence_{0};
    std::atomic<std::uint8_t> statusState_{
        static_cast<std::uint8_t>(RuntimeState::Stopped)};
    std::atomic<std::uint8_t> statusFailure_{
        static_cast<std::uint8_t>(RuntimeFailure::None)};
    std::atomic<std::uint64_t> statusGeneration_{0};
    RuntimeComponentRegistry registry_{};
};

} // namespace nrfusion
