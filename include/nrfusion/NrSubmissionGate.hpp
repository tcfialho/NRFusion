#pragma once

#include <cstdint>

namespace nrfusion {

class NrSubmissionGate {
public:
    void MarkCreated(std::uint64_t submissionEpoch) noexcept;
    bool ReadyFor(std::uint64_t submissionEpoch) noexcept;
    void Reset() noexcept;

    bool Pending() const noexcept { return pending_; }
    std::uint64_t CreateEpoch() const noexcept { return createEpoch_; }

private:
    std::uint64_t createEpoch_ = 0;
    bool pending_ = false;
};

} // namespace nrfusion
