#include "nrfusion/D3D12CarrierCapabilities.hpp"

#include <cassert>

using namespace nrfusion;

int main() {
    RuntimeComponentRegistry registry;
    assert(RegisterD3D12CarrierProvider(registry));

    const auto provider = D3D12CarrierProviderComponent();
    assert(provider.kind == RuntimeComponentKind::Provider);
    assert(provider.api == GraphicsApi::D3D12);

    const auto acquire = D3D12CarrierCapabilityMask(
        D3D12CarrierCapability::Acquire);
    const auto normalize = D3D12CarrierCapabilityMask(
        D3D12CarrierCapability::Normalize);
    const auto execute = D3D12CarrierCapabilityMask(
        D3D12CarrierCapability::Execute);
    const auto compose = D3D12CarrierCapabilityMask(
        D3D12CarrierCapability::Compose);

    assert(registry.Supports({
        RuntimeComponentKind::Provider, GraphicsApi::D3D12, acquire
    }));
    assert(registry.Supports({
        RuntimeComponentKind::Provider, GraphicsApi::D3D12, normalize
    }));
    assert(registry.Supports({
        RuntimeComponentKind::Provider, GraphicsApi::D3D12,
        acquire | normalize
    }));
    assert(!registry.Supports({
        RuntimeComponentKind::Provider, GraphicsApi::D3D12, execute
    }));
    assert(!registry.Supports({
        RuntimeComponentKind::Provider, GraphicsApi::D3D12, compose
    }));
    assert(!registry.Supports({
        RuntimeComponentKind::Executor, GraphicsApi::D3D12, execute
    }));

    const auto executor = D3D12CarrierExecutorComponent();
    assert(executor.kind == RuntimeComponentKind::Executor);
    assert(executor.api == GraphicsApi::D3D12);
    assert((executor.capabilityMask & execute) == execute);
    assert((executor.capabilityMask & compose) == compose);
    assert((executor.capabilityMask & acquire) == 0);
    assert((executor.capabilityMask & normalize) == 0);

    assert(registry.Register(executor));
    assert(registry.Supports({
        RuntimeComponentKind::Executor, GraphicsApi::D3D12,
        execute | compose
    }));
    return 0;
}
