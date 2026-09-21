#pragma once

#include "nrfusion/RuntimeShell.hpp"

#include <span>

namespace nrfusion {

enum class RuntimeBootstrapFailure : std::uint8_t {
    None,
    InvalidConfig,
    InvalidComponent
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
    static void Stop(RuntimeShell& shell) noexcept;
};

} // namespace nrfusion
