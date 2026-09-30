#pragma once
#include "GameNeuralTiming.hpp"
#include "nrfusion/D3D12NrExecutor.hpp"
#include "nrfusion/PerformanceController.hpp"
#include "nrfusion/RuntimeAdvancedConfig.hpp"
#include "nrfusion/RuntimeConfig.hpp"

namespace nrfusion {
class GameNeuralControl {
public:
    void Reconfigure(const RuntimeConfig& config, const RuntimeAdvancedConfig& advanced);
    void Consume(GameNeuralTiming& timings);
    float WorkingScale() const noexcept { return scale_; }
    double GpuMilliseconds() const noexcept { return gpuMs_; }
    std::uint64_t Generation() const noexcept { return generation_; }
private:
    struct Settings {
        bool enabled = false;
        RuntimeNrMode mode = RuntimeNrMode::Auto;
        float targetFps = 60;
        NrPlacement placement = NrPlacement::Auto;
        float manualScale = 1;
        std::uint32_t passes = 1;
        bool residual = false;
        float residualBlend = 0.08f;
        DlssNrTuning appearance{};
        bool operator==(const Settings&) const noexcept = default;
    } settings_;
    PerformanceController controller_;
    float scale_ = 1;
    double gpuMs_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t lastSampleCounter_ = 0;
};
} // namespace nrfusion
