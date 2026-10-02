#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nrfusion {

struct NrDriverInterfaceObservation {
    std::uint32_t interfaceId = 0;
    std::uintptr_t functionId = 0;
    std::uint64_t observations = 0;
};

struct NrKernelLaunchShape {
    std::uint32_t gridX = 0, gridY = 0, gridZ = 0;
    std::uint32_t blockX = 0, blockY = 0, blockZ = 0;
    std::uint32_t sharedBytes = 0;
};

// NVAPI identities remain distinct from CUDA-driver handles; their lifetimes and submission APIs differ.
struct NrKernelStat {
    std::string name;
    std::string backend = "cuda_driver";
    std::string moduleHash;
    std::uintptr_t functionId = 0, moduleId = 0, deviceId = 0, queueId = 0;
    std::uint64_t generation = 0;
    NrKernelLaunchShape shape{};
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
    double unattributedMsPerFrame = 0.0;
    std::uint64_t droppedSamples = 0;    // launches that outran the event ring

    // What share of the measured time one kernel carries. This is the number that decides
    // where optimisation goes, and it is the one no static analysis can produce.
    double shareOf(const NrKernelStat& kernel) const noexcept {
        double total = measuredMsPerFrame * static_cast<double>(frames);
        if (total == 0.0)
            for (const auto& k : kernels) total += k.totalMs;
        return total > 0.0 ? kernel.totalMs / total : 0.0;
    }
};

class NrKernelProfiler {
public:
    static NrKernelProfiler& Instance();

    // Driver discovery requires NRFUSION_KERNEL_DISCOVERY=1 before feature creation.
    bool Start();
    void Stop();
    bool Running() const noexcept;

    // Call once per presented frame so per-frame figures have a denominator.
    void MarkFrame();

    NrKernelReport Report() const;
    void Reset();

    // Human-readable table, ordered by total time per frame.
    std::string FormatReport() const;

    bool StartDriverDiscovery();
    void StopDriverDiscovery();
    std::vector<NrDriverInterfaceObservation> DriverInterfaces() const;

private:
    NrKernelProfiler() = default;
};

} // namespace nrfusion
