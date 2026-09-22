#include "nrfusion/FusionRuntime.hpp"

#include <limits>

namespace nrfusion {

bool FusionRuntime::CanBeginConfigurationEpoch() const noexcept {
    const auto max = (std::numeric_limits<std::uint64_t>::max)();
    return autoExecutionGeneration_ != max && autoPrecisionGeneration_ != max;
}

void FusionRuntime::BeginConfigurationEpoch(float initialScale) {
    autoExecutionGeneration_ = NextAutoConfigurationGeneration(
        autoExecutionGeneration_, "Auto execution generation");
    autoPrecisionGeneration_ = NextAutoConfigurationGeneration(
        autoPrecisionGeneration_, "Auto precision generation");
    ResetAutoAdaptiveState(initialScale);
    haveAutoDecision_ = false;
}

} // namespace nrfusion
