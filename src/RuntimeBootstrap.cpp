#include "nrfusion/RuntimeBootstrap.hpp"

namespace nrfusion {

RuntimeBootstrapResult RuntimeBootstrap::Start(
    RuntimeShell& shell, const RuntimeBootstrapPlan& plan) noexcept {
    shell.Shutdown();
    if (!shell.Initialize(plan.config)) {
        shell.Shutdown();
        return {RuntimeBootstrapFailure::InvalidConfig};
    }

    for (const auto component : plan.components) {
        if (!shell.Registry().Register(component)) {
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
