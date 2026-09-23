#pragma once

#include "nrfusion/D3D12CarrierCapabilities.hpp"
#include "nrfusion/RuntimeBootstrap.hpp"

namespace nrfusion {

struct D3D12CarrierExecutorHooks {
    void* context = nullptr;
    bool (*qualify)(void*) noexcept = nullptr;
    void (*rollback)(void*) noexcept = nullptr;
};

enum class D3D12CarrierActivationFailure : std::uint8_t {
    None,
    RuntimeNotRunning,
    ProviderMissing,
    QualificationFailed,
    ComponentActivationFailed,
    ComponentDeactivationFailed
};

struct D3D12CarrierActivationResult {
    D3D12CarrierActivationFailure failure =
        D3D12CarrierActivationFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == D3D12CarrierActivationFailure::None;
    }
};

RuntimeBootstrapResult StartD3D12CarrierRuntime(
    RuntimeShell& shell, const RuntimeConfig& config) noexcept;

D3D12CarrierActivationResult ActivateD3D12CarrierExecutor(
    RuntimeShell& shell, const D3D12CarrierExecutorHooks& hooks) noexcept;
D3D12CarrierActivationResult DeactivateD3D12CarrierExecutor(
    RuntimeShell& shell, const D3D12CarrierExecutorHooks& hooks) noexcept;

} // namespace nrfusion
