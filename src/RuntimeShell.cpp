#include "nrfusion/RuntimeShell.hpp"

namespace nrfusion {
namespace {

RuntimeState StateFor(const RuntimeConfig& config) noexcept {
    return config.enabled ? RuntimeState::Running : RuntimeState::Disabled;
}

}

RuntimeStatus RuntimeShell::Status() const noexcept {
    for (;;) {
        const auto begin = statusSequence_.load(std::memory_order_acquire);
        if ((begin & 1u) != 0) continue;

        const RuntimeStatus snapshot{
            static_cast<RuntimeState>(statusState_.load(std::memory_order_relaxed)),
            static_cast<RuntimeFailure>(statusFailure_.load(std::memory_order_relaxed)),
            statusGeneration_.load(std::memory_order_relaxed)};

        if (begin == statusSequence_.load(std::memory_order_acquire))
            return snapshot;
    }
}

void RuntimeShell::PublishStatus(RuntimeStatus status) noexcept {
    statusSequence_.fetch_add(1, std::memory_order_acq_rel);
    statusState_.store(static_cast<std::uint8_t>(status.state), std::memory_order_relaxed);
    statusFailure_.store(static_cast<std::uint8_t>(status.failure), std::memory_order_relaxed);
    statusGeneration_.store(status.configGeneration, std::memory_order_relaxed);
    statusSequence_.fetch_add(1, std::memory_order_release);
}

bool RuntimeShell::Initialize(RuntimeConfig config) noexcept {
    const auto state = Status().state;
    if (state == RuntimeState::Running || state == RuntimeState::Disabled)
        return Reconfigure(config);
    if (!config.Valid()) {
        PublishStatus({RuntimeState::Failed, RuntimeFailure::InvalidConfig, 0});
        return false;
    }

    config_ = config;
    PublishStatus({StateFor(config_), RuntimeFailure::None, config_.generation});
    return true;
}

void RuntimeShell::Shutdown() noexcept {
    registry_.Clear();
    config_ = {};
    PublishStatus({});
}

bool RuntimeShell::Reconfigure(RuntimeConfig config) noexcept {
    const auto state = Status().state;
    if (state == RuntimeState::Stopped || state == RuntimeState::Failed)
        return Initialize(config);
    if (!config.Valid()) return false;
    if (config.generation < config_.generation) return false;
    if (config.generation == config_.generation) return config == config_;

    config_ = config;
    PublishStatus({StateFor(config_), RuntimeFailure::None, config_.generation});
    return true;
}

} // namespace nrfusion
