#include "nrfusion/TransportPolicy.hpp"

namespace nrfusion {

ProcessTransport TransportPolicy::Choose(const GameContext& game) const noexcept {
    return game.is32Bit ? ProcessTransport::X86Carrier : ProcessTransport::InProcess;
}

bool TransportPolicy::IsSupported(const GameContext& game,
                                  const RuntimeCapabilities& capabilities) const noexcept {
    return !game.is32Bit || capabilities.x86Carrier;
}

} // namespace nrfusion
