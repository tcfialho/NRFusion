#include "nrfusion/NrKernelProfile.hpp"

#include <cstdio>

namespace nrfusion {

std::string NrKernelProfiler::FormatReport() const {
    const auto report = Report();
    char row[2048]{};
    std::snprintf(row, sizeof(row), "frames=%llu kernel_ms/frame=%.6f droppedSamples=%llu\n",
        static_cast<unsigned long long>(report.frames), report.measuredMsPerFrame,
        static_cast<unsigned long long>(report.droppedSamples));
    std::string output = row;
    output += "kernel | backend | function/module/generation | calls/frame | us/call | ms/frame | share | grid | block | shared\n";
    for (const auto& kernel : report.kernels) {
        const double calls = report.frames ? double(kernel.calls) / double(report.frames) : 0;
        const double ms = report.frames ? kernel.totalMs / double(report.frames) : 0;
        const auto& shape = kernel.shape;
        std::snprintf(row, sizeof(row), "%s | %s | 0x%llx/0x%llx/%llu | %.3f | %.6f | %.6f | %.3f%% | %ux%ux%u | %ux%ux%u | %u\n",
            kernel.name.c_str(), kernel.backend.c_str(),
            static_cast<unsigned long long>(kernel.functionId), static_cast<unsigned long long>(kernel.moduleId),
            static_cast<unsigned long long>(kernel.generation), calls, kernel.meanMs() * 1000.0,
            ms, report.shareOf(kernel) * 100.0, shape.gridX, shape.gridY, shape.gridZ,
            shape.blockX, shape.blockY, shape.blockZ, shape.sharedBytes);
        output += row;
    }
    return output;
}

} // namespace nrfusion
