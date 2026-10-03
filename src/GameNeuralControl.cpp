#include "GameNeuralControl.hpp"
#include "nrfusion/AdaptiveWorkloadGate.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/Logger.hpp"
#include <cmath>

namespace nrfusion {
void GameNeuralControl::Reconfigure(const RuntimeConfig& config, const RuntimeAdvancedConfig& advanced) {
    Settings requested{};
    requested.enabled = config.enabled;
    requested.mode = config.mode;
    requested.targetFps = config.targetFps;
    requested.placement = advanced.nr.placement;
    requested.manualScale = advanced.nr.workingScale;
    requested.passes = advanced.nr.multipassEnabled ? advanced.nr.passCount : 1;
    requested.residual = advanced.nr.residualEnabled;
    requested.residualBlend = advanced.nr.residualBlend;
    requested.appearance.intensity = advanced.nr.appearance.intensity;
    requested.appearance.style = static_cast<int>(advanced.nr.appearance.style);
    requested.appearance.localStructure = advanced.nr.appearance.localStructure;
    requested.appearance.skinStructure = advanced.nr.appearance.skinStructure.value_or(-1.0f);
    requested.appearance.autoMask = advanced.nr.appearance.automaticMask != TriState::Off;
    if (generation_ && requested == settings_) return;
    const bool remainAutomatic = generation_ && settings_.mode == RuntimeNrMode::Auto && config.mode == RuntimeNrMode::Auto;
    settings_ = requested;
    ++generation_;
    gpuMs_ = 0;
    lastSampleCounter_ = 0;
    PerformanceConfig policy{};
    policy.targetFps = config.targetFps;
    controller_ = PerformanceController(policy);
    const float initial = remainAutomatic ? scale_ : 1.0f;
    controller_.Reset(initial);
    if (config.mode == RuntimeNrMode::Custom) scale_ = advanced.nr.workingScale;
    else if (config.mode == RuntimeNrMode::Performance) scale_ = 0.5f;
    else if (config.mode == RuntimeNrMode::BestQuality) scale_ = 1;
    else scale_ = controller_.WorkingScale();
}

void GameNeuralControl::Consume(GameNeuralTiming& timings) {
    static const double frequency = [] { LARGE_INTEGER counter{}; QueryPerformanceFrequency(&counter); return static_cast<double>(counter.QuadPart); }();
    GameNeuralGpuSample sample{};
    while (timings.Consume(sample)) {
        if (sample.generation != generation_ || std::abs(sample.workingScale - scale_) > 0.0001f) continue;
        gpuMs_ = sample.gpuMs;
        if (settings_.mode != RuntimeNrMode::Auto || !settings_.enabled) continue;
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        TelemetrySample telemetry{};
        telemetry.dtSeconds = lastSampleCounter_ ? (counter.QuadPart - lastSampleCounter_) / frequency : 0.05;
        lastSampleCounter_ = counter.QuadPart;
        telemetry.nrGpuMs = sample.gpuMs;
        telemetry.nrTimingFresh = true;
        telemetry.frameTimingSource = FrameTimingSource::Unknown;
        telemetry.sourceFps = DlssgTransfusion::Instance().RenderedFps();
        telemetry.processedFps = telemetry.sourceFps;
        controller_.ObserveScaleCost(scale_, sample.gpuMs);
        if (AdaptiveWorkloadGate::Instance().IsSampleValid()) {
            const auto decision = controller_.Update(telemetry);
            if (decision.changedScale) {
                NRF_LOG_INFO("NeuralAuto", "GPU NR %.3f ms; scale %.2f -> %.2f targetRendered=%.1f generation=%llu",
                    sample.gpuMs, scale_, decision.workingScale, settings_.targetFps, generation_);
                scale_ = decision.workingScale;
            }
        }
    }
}
} // namespace nrfusion
