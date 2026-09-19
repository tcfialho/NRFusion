#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nrfusion {

// Work out which argument of a vendor kernel is the input, which is the output and which are
// the weights -- by watching, not by guessing.
//
// Replacing a kernel with a faster one requires knowing what its arguments mean, and a wrong
// guess corrupts the image instead of failing loudly. The meanings are not in the binary, but
// they are visible in behaviour: an argument the launch writes to is an output; one that holds
// the same bytes for the life of the process is a weight; one that changes every frame before
// the launch reads it is an input.
//
// This is a property of the runtime, not of the game. Determined once for a given
// nvngx_dlssnr.dll it holds for every game that loads that runtime.
enum class ArgumentRole : std::uint8_t {
    Unknown,
    Output,     // the launch changed these bytes
    Weight,     // never changed, in any frame, since the first sighting
    Input,      // changed between launches, but not by the launch itself
    Scalar,     // not a device pointer: a dimension, a flag, a count
};

struct KernelArgument {
    std::size_t index = 0;
    std::size_t sizeBytes = 0;
    ArgumentRole role = ArgumentRole::Unknown;
    std::uint64_t value = 0;          // the scalar, or the device address
    std::uint64_t timesChangedByLaunch = 0;
    std::uint64_t timesChangedBetweenLaunches = 0;
    std::uint64_t observations = 0;
};

struct KernelAbi {
    std::string name;
    std::uint64_t launches = 0;
    std::vector<KernelArgument> arguments;
};

class NrKernelAbiProbe {
public:
    static NrKernelAbiProbe& Instance();

    // Watches only kernels whose name contains `filter`. Narrow it: every watched launch costs
    // a device-to-host read on both sides of the launch, which is far too expensive to leave
    // running over the whole pass.
    bool Start(const std::string& filter);
    void Stop();
    bool Running() const noexcept;

    std::vector<KernelAbi> Report() const;
    std::string FormatReport() const;
    void Reset();

private:
    NrKernelAbiProbe() = default;
};

const char* Describe(ArgumentRole role) noexcept;

} // namespace nrfusion
