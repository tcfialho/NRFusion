#include "nrfusion/RuntimeBootstrap.hpp"

namespace nrfusion {
namespace {

bool MatchesPlan(const RuntimeShell& shell, const RuntimeBootstrapPlan& plan) noexcept {
    if (!(shell.Config() == plan.config)) return false;

    RuntimeComponentRegistry expected;
    for (const auto component : plan.components)
        if (!expected.Register(component)) return false;
    if (expected.Size() != shell.Registry().Size()) return false;

    for (const auto component : plan.components) {
        const auto* expectedEntry = expected.Find(component.kind, component.api);
        const auto* activeEntry = shell.Registry().Find(component.kind, component.api);
        if (!expectedEntry || !activeEntry ||
            expectedEntry->capabilityMask != activeEntry->capabilityMask)
            return false;
    }
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

RuntimeBootstrapResult RuntimeBootstrap::ActivateComponent(
    RuntimeShell& shell, RuntimeComponent component) noexcept {
    const auto state = shell.Status().state;
    if (state != RuntimeState::Running && state != RuntimeState::Disabled)
        return {RuntimeBootstrapFailure::NotStarted};
    if (!shell.registry_.Register(component))
        return {RuntimeBootstrapFailure::InvalidComponent};
    return {};
}

RuntimeBootstrapResult RuntimeBootstrap::DeactivateComponent(
    RuntimeShell& shell, RuntimeComponent component) noexcept {
    const auto state = shell.Status().state;
    if (state != RuntimeState::Running && state != RuntimeState::Disabled)
        return {RuntimeBootstrapFailure::NotStarted};
    if (!shell.registry_.Remove(component))
        return {RuntimeBootstrapFailure::InvalidComponent};
    return {};
}

void RuntimeBootstrap::Stop(RuntimeShell& shell) noexcept {
    shell.Shutdown();
}

} // namespace nrfusion
