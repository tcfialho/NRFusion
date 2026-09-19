#pragma once

#include <vector>

namespace nrfusion {

struct GpuInterval {
    double beginMs = 0.0;
    double endMs = 0.0;
};

class AsyncOverlapEstimator {
public:
    explicit AsyncOverlapEstimator(double smoothingSeconds = 0.75);

    static double Instantaneous(const GpuInterval& nr, const std::vector<GpuInterval>& concurrent);
    double Update(const GpuInterval& nr, const std::vector<GpuInterval>& concurrent, double dtSeconds);
    void Reset() noexcept { value_ = 0.0; initialized_ = false; }
    double Value() const noexcept { return value_; }

private:
    double smoothingSeconds_;
    double value_ = 0.0;
    bool initialized_ = false;
};

} // namespace nrfusion
