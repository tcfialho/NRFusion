#include "nrfusion/RuntimeShell.hpp"

namespace nrfusion {
namespace {

RuntimeState StateFor(const RuntimeConfig& config) noexcept {
    return config.enabled ? RuntimeState::Running : RuntimeState::Disabled;
}

}

bool RuntimeShell::Initialize(RuntimeConfig config) noexcept {
    if (status_.state == RuntimeState::Running || status_.state == RuntimeState::Disabled)
        return Reconfigure(config);
    if (!config.Valid()) {
        status_ = {RuntimeState::Failed, RuntimeFailure::InvalidConfig, 0};
        return false;
    }

    config_ = config;
    status_ = {StateFor(config_), RuntimeFailure::None, config_.generation};
    return true;
}

void RuntimeShell::Shutdown() noexcept {
    registry_.Clear();
    status_ = {};
}

bool RuntimeShell::Reconfigure(RuntimeConfig config) noexcept {
    if (status_.state == RuntimeState::Stopped || status_.state == RuntimeState::Failed)
        return Initialize(config);
    if (!config.Valid()) return false;
    if (config.generation < config_.generation) return false;
    if (config.generation == config_.generation) return config == config_;

    config_ = config;
    status_ = {StateFor(config_), RuntimeFailure::None, config_.generation};
    return true;
}

} // namespace nrfusion
