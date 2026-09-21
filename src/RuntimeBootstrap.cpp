#include "nrfusion/RuntimeBootstrap.hpp"

namespace nrfusion {
namespace {

bool MatchesPlan(const RuntimeShell& shell, const RuntimeBootstrapPlan& plan) noexcept {
    if (!(shell.Config() == plan.config)) return false;
    for (const auto component : plan.components)
        if (!shell.Registry().Supports(component)) return false;
    return true;
}

}

RuntimeBootstrapResult RuntimeBootstrap::Start(
    RuntimeShell& shell, const RuntimeBootstrapPlan& plan) noexcept {
    const auto state = shell.Status().state;
    if (state == RuntimeState::Running || state == RuntimeState::Disabled) {
        return MatchesPlan(shell, plan)
            ? RuntimeBootstrapResult{}
            : RuntimeBootstrapResult{RuntimeBootstrapFailure::AlreadyStarted};
    }

    shell.Shutdown();
    if (!shell.Initialize(plan.config)) {
        shell.Shutdown();
        return {RuntimeBootstrapFailure::InvalidConfig};
    }

    for (const auto component : plan.components) {
        if (!shell.registry_.Register(component)) {
            shell.Shutdown();
            return {RuntimeBootstrapFailure::InvalidComponent};
        }
    }
    return {};
}

void RuntimeBootstrap::Stop(RuntimeShell& shell) noexcept {
    shell.Shutdown();
}

} // namespace nrfusion
