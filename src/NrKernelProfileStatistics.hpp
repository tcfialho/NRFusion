#pragma once
#include "nrfusion/NrKernelProfile.hpp"

namespace nrfusion::kernelprofile {
struct LaunchRecord;
void AggregateFrame(const LaunchRecord* records, std::size_t count,
                    const std::uint64_t* ticks, std::uint64_t frequency,
                    std::uintptr_t queue, std::uint64_t dropped);
NrKernelReport NativeReport();
void ResetNativeReport();
bool NvapiObservationEnabled() noexcept;
} // namespace nrfusion::kernelprofile
