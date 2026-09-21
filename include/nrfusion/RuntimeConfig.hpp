#pragma once

#include "nrfusion/Types.hpp"

#include <cmath>
#include <cstdint>

namespace nrfusion {

struct RuntimeConfig {
    std::uint64_t generation = 1;
    bool enabled = true;
    UserMode mode = UserMode::Auto;
    float targetFps = 60.0f;

    bool Valid() const noexcept {
        return generation != 0 && std::isfinite(targetFps) &&
               targetFps >= 1.0f && targetFps <= 1000.0f;
    }
};

} // namespace nrfusion
