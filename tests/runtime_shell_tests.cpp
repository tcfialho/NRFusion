#include "nrfusion/RuntimeShell.hpp"

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

    RuntimeConfig config;
    config.generation = 2;
    config.enabled = false;
    config.targetFps = 120.0f;
    assert(runtime.Initialize(config));
    assert(runtime.Status().state == RuntimeState::Disabled);
    assert(runtime.Status().configGeneration == 2);

    assert(runtime.Registry().Register(
        {RuntimeComponentKind::Provider, GraphicsApi::D3D12, 1}));
    assert(runtime.Registry().Register(
        {RuntimeComponentKind::Provider, GraphicsApi::D3D12, 2}));
    const auto* d3d12 = runtime.Registry().Find(
        RuntimeComponentKind::Provider, GraphicsApi::D3D12);
    assert(d3d12 && d3d12->capabilityMask == 3);

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
    return 0;
}
