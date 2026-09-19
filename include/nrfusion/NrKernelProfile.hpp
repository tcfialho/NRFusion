#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nrfusion {

// Time per kernel of the neural pass, measured from inside the running game.
//
// Three earlier readings disagreed about what limits that pass, and each was inferring from
// something other than the kernels: a roofline from weight shapes, a synthetic loop from
// instruction throughput, a published instruction mix from static counts. None of them says
// which block costs what. This does, because it times the launches as they happen.
//
// It needs no profiler installed and no capture session: it hooks the CUDA launch entry the
// vendor runtime already calls, records a pair of events around each launch, and resolves
// them a few frames later so nothing waits on the GPU.
struct NrKernelStat {
    std::string name;
    std::uint64_t calls = 0;
    double totalMs = 0.0;
    double minMs = 0.0;
    double maxMs = 0.0;

    double meanMs() const noexcept { return calls ? totalMs / static_cast<double>(calls) : 0.0; }
};

struct NrKernelReport {
    std::vector<NrKernelStat> kernels;   // heaviest first
    std::uint64_t frames = 0;
    double measuredMsPerFrame = 0.0;     // sum over kernels, divided by frames
    std::uint64_t droppedSamples = 0;    // launches that outran the event ring

    // What share of the measured time one kernel carries. This is the number that decides
    // where optimisation goes, and it is the one no static analysis can produce.
    double shareOf(const NrKernelStat& kernel) const noexcept {
        double total = 0.0;
        for (const auto& k : kernels) total += k.totalMs;
        return total > 0.0 ? kernel.totalMs / total : 0.0;
    }
};

class NrKernelProfiler {
public:
    static NrKernelProfiler& Instance();

    // Off by default and free when off: no hook is installed until this is called, so a
    // player who never asks for a measurement never pays for one.
    bool Start();
    void Stop();
    bool Running() const noexcept;

    // Call once per presented frame so per-frame figures have a denominator.
    void MarkFrame();

    NrKernelReport Report() const;
    void Reset();

    // Human-readable table, ordered by total time per frame.
    std::string FormatReport() const;

private:
    NrKernelProfiler() = default;
};

} // namespace nrfusion
