#pragma once
#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/Types.hpp"

#include <optional>

namespace nrfusion {

class ProviderPolicy {
public:
    FrameProvider Choose(const GameContext& game, const RuntimeCapabilities& capabilities,
                         std::optional<FrameProvider> preferred = std::nullopt) const noexcept;
};

} // namespace nrfusion
