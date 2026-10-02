#pragma once

#include "nrfusion/SyntheticProvider.hpp"

#include <optional>

namespace nrfusion {

std::optional<SyntheticFrameInputs> BuildD3D11CarrierWork(
    const FrameContext& frame, const WorkTicket& ticket) noexcept;

} // namespace nrfusion
