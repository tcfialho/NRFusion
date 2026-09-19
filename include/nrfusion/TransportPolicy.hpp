#pragma once
#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/Types.hpp"

namespace nrfusion {

class TransportPolicy {
public:
    ProcessTransport Choose(const GameContext& game) const noexcept;
    bool IsSupported(const GameContext& game, const RuntimeCapabilities& capabilities) const noexcept;
};

} // namespace nrfusion
