#pragma once

#include "nrfusion/WorkLedger.hpp"

#include <concepts>
#include <cstdint>
#include <optional>

namespace nrfusion {

struct NrRetiredTimingSample {
    bool mapsWork = false;
    WorkTicket ticket{};
    double gpuMs = 0.0;
};

template <class Source>
concept NrTimingSource = requires(Source& source, std::uint64_t completedValue) {
    { source.TryRetire(completedValue) } ->
        std::same_as<std::optional<NrRetiredTimingSample>>;
    { source.ResetAfterIdle() } -> std::same_as<void>;
};

}
