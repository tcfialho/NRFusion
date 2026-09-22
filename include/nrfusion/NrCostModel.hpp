#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace nrfusion {

struct NrCostFit {
    double fixedMs = 0.0;
    double areaMs = 0.0;
    double rSquared = 0.0;
    std::size_t distinctScales = 0;
    std::size_t samples = 0;
};

// Learns NR GPU cost as fixed overhead + area-scaled work:
//   ms ~= fixedMs + areaMs * scale^2
// The fixed term matters because real NR cost does not fall perfectly with pixel count.
class NrCostModel {
public:
    void Reset();
    void ReserveScales(std::size_t count);
    void Observe(float scale, double gpuMs);

    std::optional<NrCostFit> Fit() const;
    std::optional<float> PredictScaleForCost(double targetGpuMs,
                                             float minScale,
                                             float maxScale) const;

    std::size_t DistinctScales() const noexcept { return points_.size(); }
    std::size_t SampleCount() const noexcept { return totalSamples_; }

private:
    struct Point {
        float scale = 1.0f;
        double meanMs = 0.0;
        std::size_t count = 0;
        std::size_t lastSerial = 0;
    };

    Point* FindPoint(float scale);
    const Point* FindPoint(float scale) const;

    std::vector<Point> points_;
    std::size_t totalSamples_ = 0;
};

} // namespace nrfusion
