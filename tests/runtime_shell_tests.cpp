#include "nrfusion/RuntimeBootstrap.hpp"
#include "nrfusion/RuntimeShell.hpp"

#include <array>
#include <cassert>
#include <limits>

using namespace nrfusion;

int main() {
    RuntimeShell runtime;
    assert(runtime.Status().state == RuntimeState::Stopped);

    RuntimeConfig invalid;
    invalid.targetFps = std::numeric_limits<float>::quiet_NaN();
    assert(!runtime.Initialize(invalid));
    assert(runtime.Status().state == RuntimeState::Failed);
    assert(runtime.Registry().Size() == 0);

    invalid = {};
    invalid.mode = static_cast<RuntimeNrMode>(255);
    assert(!runtime.Initialize(invalid));

    RuntimeConfig config;
    config.generation = 2;
    config.enabled = false;
    config.targetFps = 120.0f;
    assert(runtime.Initialize(config));
    assert(runtime.Status().state == RuntimeState::Disabled);
    assert(runtime.Status().configGeneration == 2);

    RuntimeComponentRegistry registry;
    assert(!registry.Register(
        {static_cast<RuntimeComponentKind>(255), GraphicsApi::D3D12, 1}));
    assert(!registry.Register(
        {RuntimeComponentKind::Provider, static_cast<GraphicsApi>(255), 1}));
    assert(registry.Register({RuntimeComponentKind::Provider, GraphicsApi::D3D12, 1}));
    assert(registry.Register({RuntimeComponentKind::Provider, GraphicsApi::D3D12, 2}));
    const auto* d3d12 = registry.Find(RuntimeComponentKind::Provider, GraphicsApi::D3D12);
    assert(d3d12 && d3d12->capabilityMask == 3);
    assert(registry.Supports({RuntimeComponentKind::Provider, GraphicsApi::D3D12, 3}));

    RuntimeConfig sameGeneration = config;
    sameGeneration.enabled = true;
    assert(!runtime.Reconfigure(sameGeneration));
    assert(runtime.Status().state == RuntimeState::Disabled);

    assert(runtime.Reconfigure(config));

    RuntimeConfig stale = config;
    stale.generation = 1;
    assert(!runtime.Reconfigure(stale));
    assert(runtime.Config().generation == 2);

    RuntimeConfig enabled = config;
    enabled.generation = 3;
    enabled.enabled = true;
    assert(runtime.Reconfigure(enabled));
    assert(runtime.Status().state == RuntimeState::Running);

    runtime.Shutdown();
    runtime.Shutdown();
    assert(runtime.Status().state == RuntimeState::Stopped);
    assert(runtime.Registry().Size() == 0);
    assert(runtime.Config().generation == 1);

    std::array<RuntimeComponent, 2> components{{
        {RuntimeComponentKind::Provider, GraphicsApi::D3D12, 1},
        {RuntimeComponentKind::Executor, GraphicsApi::D3D12, 2},
    }};
    RuntimeBootstrapPlan plan{enabled, components};
    const auto started = RuntimeBootstrap::Start(runtime, plan);
    assert(started);
    assert(runtime.Status().state == RuntimeState::Running);
    assert(runtime.Registry().Size() == 2);
    assert(RuntimeBootstrap::Start(runtime, plan));

    auto differentPlan = plan;
    differentPlan.config.generation = 4;
    const auto duplicate = RuntimeBootstrap::Start(runtime, differentPlan);
    assert(!duplicate);
    assert(duplicate.failure == RuntimeBootstrapFailure::AlreadyStarted);
    assert(runtime.Config().generation == 3);

    RuntimeBootstrap::Stop(runtime);

    std::array<RuntimeComponent, 2> badComponents{{
        {RuntimeComponentKind::Provider, GraphicsApi::D3D12, 1},
        {RuntimeComponentKind::Executor, GraphicsApi::Unknown, 2},
    }};
    plan.components = badComponents;
    const auto failed = RuntimeBootstrap::Start(runtime, plan);
    assert(!failed);
    assert(failed.failure == RuntimeBootstrapFailure::InvalidComponent);
    assert(runtime.Status().state == RuntimeState::Stopped);
    assert(runtime.Registry().Size() == 0);

    RuntimeBootstrap::Stop(runtime);
    RuntimeBootstrap::Stop(runtime);
    assert(runtime.Status().state == RuntimeState::Stopped);
    return 0;
}
