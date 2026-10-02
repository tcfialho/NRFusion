#pragma once

#include "nrfusion/RuntimeShell.hpp"

#include <cstdint>
#include <span>

namespace nrfusion {

enum class RuntimeBootstrapFailure : std::uint8_t {
    None,
    InvalidConfig,
    InvalidComponent,
    AlreadyStarted,
    NotStarted
};

struct RuntimeBootstrapPlan {
    RuntimeConfig config{};
    std::span<const RuntimeComponent> components{};
};

struct RuntimeBootstrapResult {
    RuntimeBootstrapFailure failure = RuntimeBootstrapFailure::None;

    constexpr explicit operator bool() const noexcept {
        return failure == RuntimeBootstrapFailure::None;
    }
};

class RuntimeBootstrap {
public:
    static RuntimeBootstrapResult Start(
        RuntimeShell& shell, const RuntimeBootstrapPlan& plan) noexcept;
    static RuntimeBootstrapResult ActivateComponent(
        RuntimeShell& shell, RuntimeComponent component) noexcept;
    static RuntimeBootstrapResult DeactivateComponent(
        RuntimeShell& shell, RuntimeComponent component) noexcept;
    static void Stop(RuntimeShell& shell) noexcept;
};

} // namespace nrfusion
