#include "nrfusion/D3D12CarrierBootstrap.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

struct FakeExecutor {
    bool qualifyResult = false;
    int qualifyCalls = 0;
    int rollbackCalls = 0;
};

bool Qualify(void* context) noexcept {
    auto& fake = *static_cast<FakeExecutor*>(context);
    ++fake.qualifyCalls;
    return fake.qualifyResult;
}

void Rollback(void* context) noexcept {
    ++static_cast<FakeExecutor*>(context)->rollbackCalls;
}

bool SupportsExecutor(const RuntimeShell& shell) {
    return shell.Registry().Supports(D3D12CarrierExecutorComponent());
}

} // namespace

int main() {
    RuntimeShell shell;
    FakeExecutor fake{};
    const D3D12CarrierExecutorHooks hooks{&fake, Qualify, Rollback};

    const auto stopped = ActivateD3D12CarrierExecutor(shell, hooks);
    assert(!stopped);
    assert(stopped.failure ==
           D3D12CarrierActivationFailure::RuntimeNotRunning);
    assert(fake.qualifyCalls == 0);
    assert(!SupportsExecutor(shell));

    RuntimeConfig invalid{};
    invalid.generation = 0;
    assert(!StartD3D12CarrierRuntime(shell, invalid));
    assert(shell.Status().state == RuntimeState::Stopped);
    assert(shell.Registry().Size() == 0);

    RuntimeConfig disabled{};
    disabled.generation = 2;
    disabled.enabled = false;
    assert(StartD3D12CarrierRuntime(shell, disabled));
    assert(shell.Status().state == RuntimeState::Disabled);
    assert(shell.Registry().Supports(D3D12CarrierProviderComponent()));
    assert(!SupportsExecutor(shell));

    const auto disabledActivation =
        ActivateD3D12CarrierExecutor(shell, hooks);
    assert(!disabledActivation);
    assert(disabledActivation.failure ==
           D3D12CarrierActivationFailure::RuntimeNotRunning);
    assert(fake.qualifyCalls == 0);
    assert(!SupportsExecutor(shell));

    RuntimeBootstrap::Stop(shell);

    RuntimeConfig enabled{};
    enabled.generation = 3;
    enabled.enabled = true;
    assert(StartD3D12CarrierRuntime(shell, enabled));
    assert(shell.Status().state == RuntimeState::Running);
    assert(shell.Registry().Size() == 1);
    assert(shell.Registry().Supports(D3D12CarrierProviderComponent()));
    assert(!SupportsExecutor(shell));

    const auto failedQualification =
        ActivateD3D12CarrierExecutor(shell, hooks);
    assert(!failedQualification);
    assert(failedQualification.failure ==
           D3D12CarrierActivationFailure::QualificationFailed);
    assert(fake.qualifyCalls == 1);
    assert(fake.rollbackCalls == 1);
    assert(shell.Registry().Size() == 1);
    assert(!SupportsExecutor(shell));

    fake.qualifyResult = true;
    const auto activated = ActivateD3D12CarrierExecutor(shell, hooks);
    assert(activated);
    assert(fake.qualifyCalls == 2);
    assert(fake.rollbackCalls == 1);
    assert(shell.Registry().Size() == 2);
    assert(SupportsExecutor(shell));

    const auto repeated = ActivateD3D12CarrierExecutor(shell, hooks);
    assert(repeated);
    assert(fake.qualifyCalls == 2);
    assert(shell.Registry().Size() == 2);

    RuntimeBootstrap::Stop(shell);
    assert(!SupportsExecutor(shell));
    assert(shell.Registry().Size() == 0);

    const auto activateStopped =
        RuntimeBootstrap::ActivateComponent(
            shell, D3D12CarrierExecutorComponent());
    assert(!activateStopped);
    assert(activateStopped.failure == RuntimeBootstrapFailure::NotStarted);
    return 0;
}
