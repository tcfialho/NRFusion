#include "nrfusion/D3D12CarrierBootstrap.hpp"

#include <array>
#include <span>

namespace nrfusion {

RuntimeBootstrapResult StartD3D12CarrierRuntime(
    RuntimeShell& shell, const RuntimeConfig& config) noexcept {
    const auto state = shell.Status().state;
    const auto provider = D3D12CarrierProviderComponent();
    if (state == RuntimeState::Running || state == RuntimeState::Disabled) {
        if (shell.Config() == config && shell.Registry().Supports(provider))
            return {};
        return {RuntimeBootstrapFailure::AlreadyStarted};
    }

    const std::array<RuntimeComponent, 1> components{{provider}};
    return RuntimeBootstrap::Start(
        shell, RuntimeBootstrapPlan{config, components});
}

D3D12CarrierActivationResult ActivateD3D12CarrierExecutor(
    RuntimeShell& shell, const D3D12CarrierExecutorHooks& hooks) noexcept {
    if (shell.Status().state != RuntimeState::Running)
        return {D3D12CarrierActivationFailure::RuntimeNotRunning};

    const auto provider = D3D12CarrierProviderComponent();
    if (!shell.Registry().Supports(provider))
        return {D3D12CarrierActivationFailure::ProviderMissing};

    const auto executor = D3D12CarrierExecutorComponent();
    if (shell.Registry().Supports(executor)) return {};

    if (hooks.qualify == nullptr)
        return {D3D12CarrierActivationFailure::QualificationFailed};
    if (!hooks.qualify(hooks.context)) {
        if (hooks.rollback != nullptr) hooks.rollback(hooks.context);
        return {D3D12CarrierActivationFailure::QualificationFailed};
    }

    const auto activated =
        RuntimeBootstrap::ActivateComponent(shell, executor);
    if (!activated) {
        if (hooks.rollback != nullptr) hooks.rollback(hooks.context);
        return {D3D12CarrierActivationFailure::ComponentActivationFailed};
    }
    return {};
}

D3D12CarrierActivationResult DeactivateD3D12CarrierExecutor(
    RuntimeShell& shell, const D3D12CarrierExecutorHooks& hooks) noexcept {
    const auto state = shell.Status().state;
    if (state != RuntimeState::Running && state != RuntimeState::Disabled)
        return {D3D12CarrierActivationFailure::RuntimeNotRunning};

    const auto executor = D3D12CarrierExecutorComponent();
    if (!shell.Registry().Supports(executor)) return {};

    const auto deactivated =
        RuntimeBootstrap::DeactivateComponent(shell, executor);
    if (!deactivated)
        return {D3D12CarrierActivationFailure::ComponentDeactivationFailed};

    if (hooks.rollback != nullptr) hooks.rollback(hooks.context);
    return {};
}

} // namespace nrfusion
