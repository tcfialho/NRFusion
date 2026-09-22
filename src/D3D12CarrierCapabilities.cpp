#include "nrfusion/D3D12CarrierCapabilities.hpp"

namespace nrfusion {

RuntimeComponent D3D12CarrierProviderComponent() noexcept {
    return {
        RuntimeComponentKind::Provider,
        GraphicsApi::D3D12,
        D3D12CarrierCapabilityMask(D3D12CarrierCapability::Acquire) |
            D3D12CarrierCapabilityMask(D3D12CarrierCapability::Normalize)
    };
}

bool RegisterD3D12CarrierProvider(RuntimeComponentRegistry& registry) noexcept {
    return registry.Register(D3D12CarrierProviderComponent());
}

} // namespace nrfusion
