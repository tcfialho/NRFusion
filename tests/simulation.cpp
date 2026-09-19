#include "nrfusion/PerformanceController.hpp"
#include "nrfusion/Presets.hpp"
#include <iomanip>
#include <iostream>

using namespace nrfusion;

int main() {
    auto cfg = MakePerformanceConfig(PerformancePreset::Aggressive, 120.0);
    PerformanceController c(cfg);
    double nr = 5.5;
    double frame = 11.0;
    for (int i = 0; i < 600; ++i) {
        // Approximate quadratic NR scaling with resolution; not a benchmark, just controller validation.
        const double scale = static_cast<double>(c.WorkingScale());
        const double modeledNr = nr * scale * scale;
        const double modeledFrame = frame - nr + modeledNr;
        TelemetrySample s;
        s.dtSeconds = 1.0 / 60.0;
        s.nrGpuMs = modeledNr;
        s.frameGpuMs = modeledFrame;
        s.sourceFps = 1000.0 / modeledFrame;
        s.processedFps = s.sourceFps;
        s.queuePressure = modeledFrame > (1000.0 / cfg.targetFps) ? 0.85 : 0.15;
        const auto d = c.Update(s);
        if (d.changedScale) {
            std::cout << std::fixed << std::setprecision(2)
                      << "t=" << i / 60.0 << "s scale=" << d.workingScale
                      << " nr=" << d.effectiveNrCriticalMs << "ms frame=" << modeledFrame << "ms\n";
        }
    }
    std::cout << "final scale=" << c.WorkingScale() << "\n";
    return 0;
}
