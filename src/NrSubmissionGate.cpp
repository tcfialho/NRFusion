#include "nrfusion/NrSubmissionGate.hpp"

namespace nrfusion {

void NrSubmissionGate::MarkCreated(std::uint64_t submissionEpoch) noexcept {
    createEpoch_ = submissionEpoch;
    pending_ = true;
}

bool NrSubmissionGate::ReadyFor(std::uint64_t submissionEpoch) noexcept {
    if (!pending_) return true;
    if (submissionEpoch <= createEpoch_) return false;

    pending_ = false;
    return true;
}

void NrSubmissionGate::Reset() noexcept {
    createEpoch_ = 0;
    pending_ = false;
}

} // namespace nrfusion
