#pragma once

#include "nrfusion/RuntimeComponentRegistry.hpp"

#include <cstdint>

namespace nrfusion {

enum class D3D12CarrierCapability : std::uint32_t {
    Acquire = 1u << 0,
    Normalize = 1u << 1,
    Execute = 1u << 2,
    Compose = 1u << 3
};

constexpr std::uint32_t D3D12CarrierCapabilityMask(
    D3D12CarrierCapability capability) noexcept {
    return static_cast<std::uint32_t>(capability);
}

RuntimeComponent D3D12CarrierProviderComponent() noexcept;
bool RegisterD3D12CarrierProvider(RuntimeComponentRegistry& registry) noexcept;

} // namespace nrfusion
