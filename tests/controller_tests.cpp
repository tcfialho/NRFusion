#include "nrfusion/AutoTuneCoordinator.hpp"
#include "nrfusion/CompatibilityDatabase.hpp"
#include "nrfusion/Diagnostics.hpp"
#include "nrfusion/ProfileStore.hpp"
#include "nrfusion/ResidualEngine.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/D3D12QueueClockBridge.hpp"
#include "nrfusion/D3D12AsyncFenceBridge.hpp"
#include "nrfusion/FusionRuntime.hpp"
#include "nrfusion/OptiScalerAdapter.hpp"
#include "nrfusion/Presets.hpp"
#include "nrfusion/TemporalConfidence.hpp"
#include "nrfusion/MgpuPlanner.hpp"
#include "nrfusion/NvofPolicy.hpp"
#include "nrfusion/TelemetryTracker.hpp"
#include "nrfusion/GuideValidation.hpp"
#include "nrfusion/MotionNormalization.hpp"
#include "nrfusion/MotionConfidence.hpp"
#include "nrfusion/TemporalHistoryRegistry.hpp"
#include "nrfusion/PipelinedExecutorState.hpp"
#include "nrfusion/Dlss5NeuralRendering.hpp"
#include "nrfusion/AdaptiveExposure.hpp"
#include "nrfusion/AdaptiveExposureController.hpp"
#include "nrfusion/QualityValidator.hpp"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

using namespace nrfusion;

static ExposureMeasurement exposureMeasurement(ExposureSource source, float whitePoint, float confidence,
                                                bool valid = true, bool sameFrame = false, FrameId frameId = 1) {
    ExposureMeasurement m;
    m.source = source;
    m.frameId = frameId;
    m.whitePoint = whitePoint;
    m.confidence = confidence;
    m.valid = valid;
    m.sameFrame = sameFrame;
    return m;
}

static TelemetrySample sample(double nr, double frame, double source, double processed, double queue, double dt = 1.0/60.0) {
    TelemetrySample s;
    s.dtSeconds = dt;
    s.nrGpuMs = nr;
    s.frameGpuMs = frame;
    s.sourceFps = source;
    s.processedFps = processed;
    s.queuePressure = queue;
    return s;
}

int main() {
    {
        PerformanceConfig extreme;
        extreme.targetFps = std::numeric_limits<double>::max();
        PerformanceController bounded(extreme);
        assert(bounded.Config().targetFps == 1000.0);
        const auto preset = MakePerformanceConfig(PerformancePreset::Auto, std::numeric_limits<double>::max());
        assert(preset.targetFps == 1000.0);

        const auto adaAuto = MakePerformanceConfig(PerformancePreset::Auto, 60.0);
        assert(adaAuto.maxNrBudgetMs >= 5.0);
        assert(adaAuto.scaleDownSustainSeconds >= 1.5);
        assert(adaAuto.nrOverBudgetRatio >= 1.20);

        AutoTuneConfig tuneCfg;
        tuneCfg.targetFps = std::numeric_limits<double>::max();
        AutoTuneCoordinator boundedTune(tuneCfg);
        RuntimeCapabilities tuneCaps; tuneCaps.fp8 = true;
        boundedTune.Start(tuneCaps);
        assert(boundedTune.State() != AutoTuneState::Idle);
    }

    {
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.nrBudgetMs = 3.0;
        cfg.scaleDownSustainSeconds = 0.5;
        cfg.cooldownSeconds = 0.2;
        PerformanceController c(cfg);
        for (int i = 0; i < 80; ++i) c.Update(sample(5.2, 10.5, 130, 92, 0.92));
        assert(c.WorkingScale() < 1.0f);
    }

    {
        // Presentation cadence is not GPU occupancy. A displayed-frame interval above the target
        // budget must not force a resolution drop when the measured NR pass itself is healthy.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        TelemetrySample presentation = sample(1.0, 100.0, 120.0, 120.0, 0.0);
        presentation.frameTimingSource = FrameTimingSource::PresentationInterval;
        for (int i = 0; i < 120; ++i) c.Update(presentation);
        assert(std::fabs(c.WorkingScale() - 1.0f) < 1e-4f);
        assert(c.Update(presentation).state == AdaptiveState::Stable);

        // A real GPU timestamp remains visible to diagnostics even when the NR contribution is too
        // small to justify spending image quality on a frame overrun caused elsewhere.
        PerformanceController gpuController(cfg);
        presentation.frameTimingSource = FrameTimingSource::GpuTimestamp;
        PerformanceDecision gpuDecision{};
        for (int i = 0; i < 8; ++i) gpuDecision = gpuController.Update(presentation);
        assert(gpuDecision.gpuFrameTimingAvailable);
        assert(std::fabs(gpuController.WorkingScale() - 1.0f) < 1e-4f);
    }

    {
        // With no GPU frame timestamp, recovery is governed by NR/queue telemetry and is not
        // permanently blocked by a presentation interval near the target frame budget.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.scaleUpSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        c.Reset(0.67f);
        TelemetrySample presentation = sample(1.0, 8.4, 120.0, 120.0, 0.0);
        presentation.frameTimingSource = FrameTimingSource::PresentationInterval;
        for (int i = 0; i < 120; ++i) c.Update(presentation);
        assert(c.WorkingScale() > 0.67f);
    }

    {
        // One delayed NR timestamp must not become a sustained overload. Two further healthy
        // samples establish the new epoch median; a real sustained overload still scales down.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.nrWarmupSamples = 3;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        TelemetrySample t = sample(1.0, 7.0, 120.0, 120.0, 0.0);
        c.Update(t);
        t.nrGpuMs = 20.0;
        c.Update(t);
        t.nrGpuMs = 1.0;
        c.Update(t);
        for (int i = 0; i < 120; ++i) c.Update(t);
        assert(std::fabs(c.WorkingScale() - 1.0f) < 1e-4f);

        t.nrGpuMs = 20.0;
        bool changed = false;
        for (int i = 0; i < 120 && !changed; ++i)
            changed = c.Update(t).changedScale;
        assert(changed && c.WorkingScale() < 1.0f);
    }

    {
        // Do not spend image quality on a bottleneck outside NR. Queue pressure, throughput loss,
        // and a GPU frame overrun with a small NR contribution belong to the source governor.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        TelemetrySample congested = sample(1.0, 100.0, 120.0, 60.0, 0.95);
        for (int i = 0; i < 180; ++i) c.Update(congested);
        assert(std::fabs(c.WorkingScale() - 1.0f) < 1e-4f);
        assert(c.Update(congested).state == AdaptiveState::ExternalPressure);

        // Once NR itself is the over-budget contributor, the same controller may reduce scale.
        congested.nrGpuMs = 6.0;
        congested.frameGpuMs = 7.0;
        congested.processedFps = 120.0;
        congested.queuePressure = 0.0;
        bool changed = false;
        for (int i = 0; i < 180 && !changed; ++i)
            changed = c.Update(congested).changedScale;
        assert(changed && c.WorkingScale() < 1.0f);
    }

    {
        // Replaying the last completed timestamp without a fresh marker must not accumulate a
        // second overload decision. A later fresh stream is still allowed to reduce the scale.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        TelemetrySample t = sample(6.0, 7.0, 120.0, 120.0, 0.0);
        for (int i = 0; i < 3; ++i) c.Update(t);
        t.nrTimingFresh = false;
        for (int i = 0; i < 180; ++i) c.Update(t);
        assert(std::fabs(c.WorkingScale() - 1.0f) < 1e-4f);

        t.nrTimingFresh = true;
        bool changed = false;
        for (int i = 0; i < 180 && !changed; ++i)
            changed = c.Update(t).changedScale;
        assert(changed && c.WorkingScale() < 1.0f);
    }

    {
        // Once the controller is warm, an isolated timer/driver stall is clipped by the short
        // median window. A genuinely sustained high-cost stream still reduces the scale.
        PerformanceConfig cfg;
        cfg.targetFps = 60.0;
        cfg.automaticNrBudget = false;
        cfg.nrBudgetMs = 3.0;
        cfg.nrOverBudgetRatio = 1.05;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        for (int i = 0; i < 30; ++i) c.Update(sample(2.5, 8.0, 60.0, 60.0, 0.0));
        c.Update(sample(40.0, 8.0, 60.0, 60.0, 0.0)); // one isolated stall
        for (int i = 0; i < 30; ++i) c.Update(sample(2.5, 8.0, 60.0, 60.0, 0.0));
        assert(std::fabs(c.WorkingScale() - 1.0f) < 1e-4f);

        bool changed = false;
        for (int i = 0; i < 180 && !changed; ++i)
            changed = c.Update(sample(5.0, 8.0, 60.0, 60.0, 0.0)).changedScale;
        assert(changed && c.WorkingScale() < 1.0f);
    }

    {
        PerformanceConfig cfg;
        cfg.targetFps = 60.0;
        cfg.nrBudgetMs = 4.0;
        cfg.scaleUpSustainSeconds = 0.5;
        cfg.cooldownSeconds = 0.1;
        PerformanceController c(cfg);
        c.Reset(0.67f);
        for (int i = 0; i < 180; ++i) c.Update(sample(1.0, 7.0, 90, 90, 0.05));
        assert(c.WorkingScale() > 0.67f);
    }


    {
        // Real NR cost has a fixed component, so pure area scaling is not enough. The learned
        // model should recover fixed + area cost and predict the scale needed for a target GPU cost.
        NrCostModel model;
        for (int i = 0; i < 4; ++i) {
            model.Observe(1.0f, 5.0);   // 1ms fixed + 4ms * 1.0^2
            model.Observe(0.75f, 3.25); // 1ms fixed + 4ms * 0.75^2
            model.Observe(0.50f, 2.0);  // 1ms fixed + 4ms * 0.50^2
        }
        const auto fit = model.Fit();
        assert(fit);
        assert(std::fabs(fit->fixedMs - 1.0) < 0.05);
        assert(std::fabs(fit->areaMs - 4.0) < 0.05);
        assert(fit->rSquared > 0.99);
        const auto predicted = model.PredictScaleForCost(3.0, 0.5f, 1.0f);
        assert(predicted && std::fabs(*predicted - std::sqrt(0.5f)) < 0.02f);
        const auto before = model.SampleCount();
        model.Observe(1.0f, 30.0); // one-frame stall/outlier must not train the model
        assert(model.SampleCount() == before);
    }

    {
        // Once two scale measurements exist, predictive downshift uses the learned fixed overhead.
        // With 1ms fixed + 4ms*area, a ~2.76ms target needs about 0.66x, not the ~0.74x predicted
        // by a zero-overhead sqrt ratio.
        PerformanceConfig cfg;
        cfg.automaticNrBudget = false;
        cfg.nrBudgetMs = 3.0;
        cfg.predictiveTargetUtilization = 0.92;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        cfg.maxPredictiveStepDrop = 4;
        cfg.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f, 0.58f, 0.50f};
        PerformanceController c(cfg);
        for (int i = 0; i < 3; ++i) {
            c.ObserveScaleCost(1.0f, 5.0);
            c.ObserveScaleCost(0.75f, 3.25);
        }
        for (int i = 0; i < 12; ++i)
            c.Update(sample(5.0, 8.0, 120.0, 120.0, 0.0, 1.0 / 60.0));
        assert(c.WorkingScale() <= 0.67f + 1e-4f);
    }


    {
        // After a scale change, stale EMA/frame pressure must not trigger another rung change until
        // the new scale has produced at least one valid NR timing.
        PerformanceConfig cfg;
        cfg.automaticNrBudget = false;
        cfg.nrBudgetMs = 3.0;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 0.0;
        cfg.maxPredictiveStepDrop = 1;
        cfg.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f};
        PerformanceController c(cfg);
        bool changed = false;
        for (int i = 0; i < 20 && !changed; ++i)
            changed = c.Update(sample(5.0, 10.0, 120.0, 120.0, 0.0, 1.0 / 60.0)).changedScale;
        assert(changed);
        const float firstDrop = c.WorkingScale();
        assert(firstDrop < 1.0f);
        for (int i = 0; i < 300; ++i)
            c.Update(sample(0.0, 12.0, 120.0, 120.0, 0.0, 1.0 / 60.0));
        assert(std::fabs(c.WorkingScale() - firstDrop) < 1e-4f);
        // Once current-generation timing arrives, adaptation may resume normally.
        for (int i = 0; i < 12; ++i)
            c.Update(sample(5.0, 10.0, 120.0, 120.0, 0.0, 1.0 / 60.0));
        assert(c.WorkingScale() < firstDrop);
    }

    {
        PerformanceController c;
        PerformanceDecision d{};
        for (int i = 0; i < 100; ++i) d = c.Update(sample(3.0, 9.0, 160, 80, 0.95));
        assert(d.recommendedSourceCapFps > 0.0);
        assert(d.recommendedSourceCapFps < 100.0);
    }

    {
        // Non-finite host telemetry/config must not poison the adaptive controller for the session.
        PerformanceConfig cfg;
        cfg.targetFps = std::numeric_limits<double>::quiet_NaN();
        cfg.queueHigh = std::numeric_limits<double>::quiet_NaN();
        cfg.queueLow = std::numeric_limits<double>::quiet_NaN();
        cfg.minScale = std::numeric_limits<float>::quiet_NaN();
        cfg.maxScale = std::numeric_limits<float>::quiet_NaN();
        PerformanceController c(cfg);
        TelemetrySample t = sample(3.0, 8.0, 120.0, 120.0, 0.0);
        t.queuePressure = std::numeric_limits<double>::quiet_NaN();
        t.asyncOverlap = std::numeric_limits<double>::quiet_NaN();
        const auto d = c.Update(t);
        assert(std::isfinite(d.workingScale));
        assert(std::isfinite(d.effectiveNrCriticalMs));
        assert(std::isfinite(d.resolvedNrBudgetMs));

        const float before = c.WorkingScale();
        assert(!c.ReportScaleBuildFailure(std::numeric_limits<float>::quiet_NaN()));
        assert(!c.IsScaleBuildFailed(std::numeric_limits<float>::quiet_NaN()));
        assert(c.WorkingScale() == before);
        c.Reset(std::numeric_limits<float>::quiet_NaN());
        assert(std::isfinite(c.WorkingScale()));
        // A free-form custom failure must not poison the nearest preset rung.
        assert(!c.ReportScaleBuildFailure(0.62f));
        assert(!c.IsScaleBuildFailed(0.62f));
        assert(!c.IsScaleBuildFailed(0.58f));
        assert(!c.IsScaleBuildFailed(0.67f));
    }

    {
        // Failing the highest/native rung has no safer upward fallback and must not underflow
        // the size_t search index while proving that fact.
        PerformanceConfig cfg;
        cfg.scaleSteps = {1.0f, 0.85f, 0.75f};
        cfg.minScale = 0.75f;
        PerformanceController c(cfg);
        c.Reset(1.0f);
        assert(!c.ReportScaleBuildFailure(1.0f));
        assert(c.IsScaleBuildFailed(1.0f));
        assert(std::fabs(c.WorkingScale() - 1.0f) < 0.001f);
    }

    {
        SchedulerPolicy policy;
        TelemetrySample s = sample(5.0, 9.0, 100, 100, 0.2);
        s.asyncOverlap = 0.35;
        assert(policy.Choose(s) == SchedulerMode::Serialized); // overlap alone cannot invent a backend
        s.asyncComputeAvailable = true;
        s.asyncComputeStable = true;
        assert(policy.Choose(s) == SchedulerMode::AsyncCompute);
        s.secondaryGpuAvailable = true;
        s.secondaryGpuStable = true;
        s.secondaryNrGpuMs = 1.5;
        s.crossAdapterMs = 0.6;
        assert(policy.Choose(s) == SchedulerMode::SecondaryGpu);
        s.asyncComputeAvailable = false;
        s.asyncComputeStable = false;
        assert(policy.Choose(s, SchedulerMode::AsyncCompute) == SchedulerMode::Serialized);
        // Stale overlap from a no-longer-usable async path must not make the same-GPU baseline
        // look artificially cheap and suppress a genuinely better secondary-GPU route.
        s.asyncOverlap = 0.95;
        assert(policy.Choose(s) == SchedulerMode::SecondaryGpu);
        // Explicit MGPU request follows the same safe degradation chain: MGPU -> Async -> Serialized.
        s.secondaryGpuStable = false;
        s.asyncComputeAvailable = true;
        s.asyncComputeStable = true;
        s.asyncOverlap = 0.35;
        assert(policy.Choose(s, SchedulerMode::SecondaryGpu) == SchedulerMode::AsyncCompute);
        s.asyncComputeStable = false;
        assert(policy.Choose(s, SchedulerMode::SecondaryGpu) == SchedulerMode::Serialized);
    }

    {
        SchedulerConfig cfg;
        cfg.minAsyncOverlap = std::numeric_limits<double>::quiet_NaN();
        cfg.secondaryGpuRequiredGain = std::numeric_limits<double>::quiet_NaN();
        cfg.maxCrossAdapterMs = std::numeric_limits<double>::quiet_NaN();
        SchedulerPolicy policy(cfg);
        TelemetrySample s;
        s.nrGpuMs = 4.0;
        s.asyncOverlap = std::numeric_limits<double>::quiet_NaN();
        s.asyncComputeAvailable = true;
        s.asyncComputeStable = true;
        assert(policy.Choose(s) == SchedulerMode::Serialized);
        s.secondaryGpuAvailable = true;
        s.secondaryGpuStable = true;
        s.secondaryNrGpuMs = 1.0;
        s.crossAdapterMs = std::numeric_limits<double>::quiet_NaN();
        assert(policy.Choose(s) != SchedulerMode::SecondaryGpu);
    }

    {
        PipelinePolicy p;
        RuntimeCapabilities pipelineCaps;
        pipelineCaps.nativeProvider = true;
        pipelineCaps.bridgeProvider = true;
        pipelineCaps.syntheticD3D12 = true;
        pipelineCaps.syntheticD3D11Bridge = true;
        pipelineCaps.syntheticVulkan = true;
        pipelineCaps.x86Carrier = true;
        pipelineCaps.preSr = true;
        pipelineCaps.acrossRr = true;
        pipelineCaps.postSr = true;
        pipelineCaps.nativeMotion = true;
        pipelineCaps.dlssContractMotion = true;
        pipelineCaps.shaderMotion = true;
        pipelineCaps.fp8 = true;
        const auto choose = [&](const GameContext& game, const FrameContext& frame, bool nvofAvailable) {
            auto caps = pipelineCaps;
            caps.nvof = nvofAvailable;
            return p.Choose(game, frame, caps);
        };

        GameContext game{GraphicsApi::D3D12, false, true, false, false};
        FrameContext f;
        f.api = game.api;
        f.renderResolution = {1920, 1080};
        f.outputResolution = {3840, 2160};
        f.motionVectors = {1, {1920, 1080}, ResourceFormat::Rg16Float};
        f.motionVectorSource = MotionSource::Native;
        f.motionVectorsReliable = true;
        const auto d = choose(game, f, true);
        assert(d.provider == FrameProvider::Native);
        assert(d.transport == ProcessTransport::InProcess);
        assert(d.api == GraphicsApi::D3D12);
        assert(d.motion == MotionSource::Native);
        assert(d.placement == NrPlacement::PreSr);

        // DLSS presence is not the same as a usable contract. If Native/Bridge are unavailable,
        // Synthetic remains a valid fallback when that provider is actually implemented.
        auto syntheticFallbackCaps = pipelineCaps;
        syntheticFallbackCaps.nativeProvider = false;
        syntheticFallbackCaps.bridgeProvider = false;
        const auto syntheticFallback = p.Choose(game, f, syntheticFallbackCaps);
        assert(syntheticFallback.provider == FrameProvider::Synthetic);
        assert(syntheticFallback.supported);

        // A present but invalid guide must not block the automatic NVOF fallback.
        f.motionVectorsReliable = false;
        const auto fallback = choose(game, f, true);
        assert(fallback.motion == MotionSource::NvidiaOpticalFlow);

        GameContext vkGame{GraphicsApi::Vulkan, false, true, true, false};
        FrameContext vk;
        vk.api = vkGame.api;
        vk.renderResolution = {1920, 1080};
        vk.outputResolution = {3840, 2160};
        const auto nativeVk = choose(vkGame, vk, false);
        assert(nativeVk.provider == FrameProvider::Native);
        assert(nativeVk.transport == ProcessTransport::InProcess);
        assert(nativeVk.api == GraphicsApi::Vulkan);
        assert(nativeVk.placement == NrPlacement::PreSr);

        // Provider and process transport are orthogonal. A 32-bit game keeps the provider
        // selected from its frame contract/API and only changes how that provider reaches x64 NR.
        GameContext x86Dx11Game{GraphicsApi::D3D11, true, true, false, false};
        FrameContext x86Dx11;
        x86Dx11.api = x86Dx11Game.api;
        const auto bridgeX86 = choose(x86Dx11Game, x86Dx11, false);
        assert(bridgeX86.provider == FrameProvider::Bridge);
        assert(bridgeX86.transport == ProcessTransport::X86Carrier);
        assert(bridgeX86.api == GraphicsApi::D3D11);

        x86Dx11Game.is32Bit = false;
        const auto bridgeInProcess = choose(x86Dx11Game, x86Dx11, false);
        assert(bridgeInProcess.provider == bridgeX86.provider);
        assert(bridgeInProcess.transport == ProcessTransport::InProcess);

        GameContext x86Dx12Game{GraphicsApi::D3D12, true, true, false, false};
        FrameContext x86Dx12;
        x86Dx12.api = x86Dx12Game.api;
        const auto nativeX86 = choose(x86Dx12Game, x86Dx12, false);
        assert(nativeX86.provider == FrameProvider::Native);
        assert(nativeX86.transport == ProcessTransport::X86Carrier);

        GameContext syntheticX86Game{GraphicsApi::Vulkan, true, false, false, false};
        FrameContext syntheticX86;
        syntheticX86.api = syntheticX86Game.api;
        const auto synthetic = choose(syntheticX86Game, syntheticX86, false);
        assert(synthetic.provider == FrameProvider::Synthetic);
        assert(synthetic.transport == ProcessTransport::X86Carrier);

        GameContext legacyGame{GraphicsApi::D3D10, true, false, false, false};
        FrameContext legacy;
        legacy.api = legacyGame.api;
        const auto unsupportedX86 = choose(legacyGame, legacy, false);
        assert(unsupportedX86.provider == FrameProvider::Unsupported);
        assert(unsupportedX86.transport == ProcessTransport::X86Carrier);

        // Motion fallback changes only the motion axis, never provider/transport/API.
        x86Dx12.motionVectors = {2, {1920, 1080}, ResourceFormat::Rg16Float};
        x86Dx12.motionVectorSource = MotionSource::Native;
        x86Dx12.motionVectorsReliable = true;
        const auto nativeMotion = choose(x86Dx12Game, x86Dx12, true);
        x86Dx12.motionVectorsReliable = false;
        const auto nvofMotion = choose(x86Dx12Game, x86Dx12, true);
        assert(nativeMotion.provider == nvofMotion.provider);
        assert(nativeMotion.transport == nvofMotion.transport);
        assert(nativeMotion.api == nvofMotion.api);
        assert(nativeMotion.motion == MotionSource::Native);
        assert(nvofMotion.motion == MotionSource::NvidiaOpticalFlow);

        // Scheduler selection is another independent axis in the combined runtime decision.
        FusionRuntime runtime;
        TelemetrySample asyncSample;
        asyncSample.nrGpuMs = 4.0;
        asyncSample.asyncOverlap = 0.4;
        asyncSample.asyncComputeAvailable = true;
        asyncSample.asyncComputeStable = true;
        auto runtimeCaps = pipelineCaps;
        runtimeCaps.nvof = true;
        runtimeCaps.asyncCompute = true;
        const auto asyncPipeline = runtime.ResolvePipeline(x86Dx12Game, x86Dx12, runtimeCaps);
        const auto asyncScheduler = runtime.ResolveScheduler(asyncSample);
        assert(asyncPipeline.provider == FrameProvider::Native);
        assert(asyncPipeline.transport == ProcessTransport::X86Carrier);
        assert(asyncScheduler == SchedulerMode::AsyncCompute);

        asyncSample.asyncComputeStable = false;
        const auto serialPipeline = runtime.ResolvePipeline(x86Dx12Game, x86Dx12, runtimeCaps);
        const auto serialScheduler = runtime.ResolveScheduler(asyncSample);
        assert(serialPipeline.provider == asyncPipeline.provider);
        assert(serialPipeline.transport == asyncPipeline.transport);
        assert(serialScheduler == SchedulerMode::Serialized);
    }

    {
        ResidualPolicy p;
        ResidualInputs r;
        r.rayReconstruction = true;
        r.hasDepth = true;
        r.motionConfidence = 0.95;
        r.disocclusionRatio = 0.05;
        const auto d = p.Decide(r);
        assert(d.useResidual && d.accumulateHistory && !d.resetHistory);
        assert(d.historyWeight > 0.7);
        r.motionConfidence = std::numeric_limits<double>::quiet_NaN();
        const auto invalid = p.Decide(r);
        assert(invalid.useResidual && invalid.resetHistory && !invalid.accumulateHistory);
        assert(std::isfinite(invalid.historyWeight));
    }

    {
        auto& a = OptiScalerAdapter::Instance();
        a.Reset(1.0f);
        AdaptiveSettings st;
        st.enabled = false;
        st.fixedScale = 0.73f;
        assert(std::fabs(a.ResolveWorkingScale(st, 10.0) - 0.73f) < 0.001f);

        st.enabled = true;
        st.mode = UserMode::Auto;
        st.targetFps = 120.0;
        // Adapter smoke-test: normal modes remain inside their preset bounds.
        const float value = a.ResolveWorkingScale(st, 5.0);
        assert(value >= 0.42f && value <= 1.0f);

        // A host resource incompatibility is session capability, not a preset preference. Preserve
        // quarantined rungs across a Target-FPS/mode controller rebuild until the host clears them.
        const auto firstFallback = a.ReportWorkingScaleBuildFailure(0.85f);
        assert(firstFallback && std::fabs(*firstFallback - 1.0f) < 0.001f);
        st.mode = UserMode::Quality;
        st.targetFps = 90.0; // forces adapter/controller reconfiguration
        a.ResolveWorkingScale(st, 5.0);
        const auto secondFallback = a.ReportWorkingScaleBuildFailure(0.75f);
        assert(secondFallback && std::fabs(*secondFallback - 1.0f) < 0.001f);
        a.Reset(1.0f);
    }

    {
        // The three adaptive modes share one ceiling and differ only in the floor. A mode is a
        // statement about how much neural resolution may be spent, not about how much is offered
        // when the frame has room to spare.
        const auto autoCfg = MakePerformanceConfig(PerformancePreset::Auto, 120.0);
        const auto qualityCfg = MakePerformanceConfig(PerformancePreset::Quality, 120.0);
        const auto perfCfg = MakePerformanceConfig(PerformancePreset::Performance, 120.0);
        assert(std::fabs(autoCfg.maxScale - 1.0f) < 1e-4f);
        assert(std::fabs(qualityCfg.maxScale - 1.0f) < 1e-4f);
        assert(std::fabs(perfCfg.maxScale - 1.0f) < 1e-4f);
        assert(perfCfg.minScale < autoCfg.minScale);
        assert(autoCfg.minScale < qualityCfg.minScale);
        for (const auto& cfg : {autoCfg, qualityCfg, perfCfg}) {
            assert(!cfg.scaleSteps.empty());
            assert(std::fabs(cfg.scaleSteps.front() - 1.0f) < 1e-4f);
            assert(std::fabs(cfg.scaleSteps.back() - cfg.minScale) < 1e-4f);
        }
    }

    {
        // Performance starts at the ceiling like the others, and a resource failure there must still
        // escape to native scale instead of stranding the host on a size it cannot build.
        auto& a = OptiScalerAdapter::Instance();
        AdaptiveSettings st; st.enabled = true; st.mode = UserMode::MaxFps; st.targetFps = 120.0;
        a.ResetForShape(st);
        assert(std::fabs(a.ResolveWorkingScale(st, 2.0) - 1.0f) < 0.001f);
        // A size the host cannot build is not evidence that an even smaller one would work:
        // alignment and format limits are not monotonic in pixel count. The escape goes toward
        // native, never further down.
        const auto fallback = a.ReportWorkingScaleBuildFailure(0.75f);
        assert(fallback && *fallback > 0.75f);
        // Clearing the quarantine only starts a new epoch when it actually moves the effective
        // scale. Here the escape stayed inside the ladder, so what must be observable is that the
        // rung is usable again after the host says the incompatibility is gone.
        a.ClearWorkingScaleBuildFailures();
        a.ResetForShape(st);
        assert(std::fabs(a.ResolveWorkingScale(st, 2.0) - 1.0f) < 0.001f);
        a.Reset(1.0f);
    }

    {
        // A different Target FPS is a different budget. Inheriting the rung chosen for the old one
        // froze the decision: the epoch restarts, the controller waits for fresh timing, and the
        // inherited rung stays until a sustained violation appears. Re-derive from the ceiling.
        auto& a = OptiScalerAdapter::Instance();
        AdaptiveSettings st; st.enabled = true; st.mode = UserMode::Auto; st.targetFps = 60.0;
        a.ResetForShape(st);
        // O adaptador deriva dt do relogio real; num laco apertado cada passo vale o minimo de
        // 1 ms, entao sao precisos alguns milhares para cobrir a janela de sustentacao do Auto.
        for (int i = 0; i < 6000; ++i)
            a.ResolveWorkingScale(st, 40.0, 40.0, 60.0, 60.0, 0.0, 0.0);
        const float loaded = a.ResolveWorkingScale(st, 40.0, 40.0, 60.0, 60.0, 0.0, 0.0);
        assert(loaded < 1.0f);
        st.targetFps = 240.0;
        assert(std::fabs(a.ResolveWorkingScale(st, 2.0) - 1.0f) < 0.001f);
        a.Reset(1.0f);
    }

    {
        const auto aggressive = MakePerformanceConfig(PerformancePreset::Aggressive, 144.0);
        assert(aggressive.minScale <= 0.35f + 0.001f);
        assert(aggressive.maxScale <= 0.75f + 0.001f);
        assert(aggressive.targetFps == 144.0);
        const auto invalidTarget = MakePerformanceConfig(
            PerformancePreset::Balanced, std::numeric_limits<double>::infinity());
        assert(std::isfinite(invalidTarget.targetFps));
        assert(invalidTarget.targetFps == 120.0);
    }

    {
        MotionNormalizationConfig pixels;
        pixels.units = MotionUnits::Pixels;
        pixels.sourceVectorDomain = {1920, 1080};
        pixels.targetVectorDomain = {3840, 2160};
        const auto doubled = MotionNormalizer::NormalizeVector(10.0f, -4.0f, pixels);
        assert(doubled && std::fabs(doubled->first - 20.0f) < 0.001f &&
               std::fabs(doubled->second + 8.0f) < 0.001f);

        MotionNormalizationConfig uv;
        uv.units = MotionUnits::NormalizedUv;
        uv.targetVectorDomain = {200, 100};
        uv.componentScaleY = -1.0;
        const auto normalized = MotionNormalizer::NormalizeVector(0.10f, 0.25f, uv);
        assert(normalized && std::fabs(normalized->first - 20.0f) < 0.001f &&
               std::fabs(normalized->second + 25.0f) < 0.001f);

        const std::vector<float> mx {1.0f, std::numeric_limits<float>::quiet_NaN(), 3.0f};
        const std::vector<float> my {2.0f, 2.0f, std::numeric_limits<float>::infinity()};
        const auto field = MotionNormalizer::NormalizeField(mx, my, pixels);
        assert(field.validShape && field.valid == std::vector<std::uint8_t>({1, 0, 0}));
        assert(std::fabs(field.validPixelRatio - (1.0 / 3.0)) < 1e-9);
        MotionNormalizationConfig invalid = pixels;
        invalid.targetVectorDomain = {};
        assert(!MotionNormalizer::NormalizeVector(1.0f, 1.0f, invalid));
        assert(!MotionNormalizer::NormalizeField({}, {}, pixels).validShape);
    }

    {
        MotionConfidenceInput input;
        input.width = 2; input.height = 1;
        input.forwardX = {0.0f, 0.0f}; input.forwardY = {0.0f, 0.0f};
        input.backwardX = {0.0f, 0.0f}; input.backwardY = {0.0f, 0.0f};
        input.currentDepth = {1.0f, 1.0f}; input.historyDepth = {1.0f, 1.0f};
        MotionConfidenceEngine engine;
        const auto perfect = engine.Evaluate(input);
        assert(perfect.validShape && perfect.valid == std::vector<std::uint8_t>({1, 1}));
        assert(perfect.meanConfidence > 0.999 && perfect.forwardBackwardAgreement > 0.999);
        input.backwardX[1] = 100.0f;
        const auto inconsistent = engine.Evaluate(input);
        assert(inconsistent.valid == std::vector<std::uint8_t>({1, 0}));
        input.backwardX[1] = 0.0f;
        input.historyDepth[1] = 10.0f;
        const auto disoccluded = engine.Evaluate(input);
        assert(disoccluded.valid == std::vector<std::uint8_t>({1, 0}));
        input.cameraCut = true;
        const auto cut = engine.Evaluate(input);
        assert(cut.validShape && cut.valid == std::vector<std::uint8_t>({0, 0}) &&
               cut.meanConfidence == 0.0);
    }

    {
        TemporalConfidence temporal;
        TemporalConfidenceInput t;
        assert(temporal.Evaluate(t).rejectHistory); // missing evidence defaults fail closed
        t.hasDepth = true;
        t.motionConfidence = 0.95;
        t.forwardBackwardAgreement = 0.92;
        t.depthAgreement = 0.97;
        t.disocclusionRatio = 0.04;
        const auto good = temporal.Evaluate(t);
        assert(good.confidence > 0.75);
        assert(!good.rejectHistory);

        t.disocclusionRatio = 0.80;
        const auto bad = temporal.Evaluate(t);
        assert(bad.rejectHistory);

        t.disocclusionRatio = 0.0;
        t.motionConfidence = std::numeric_limits<double>::quiet_NaN();
        const auto nonFinite = temporal.Evaluate(t);
        assert(nonFinite.rejectHistory);
        assert(std::isfinite(nonFinite.confidence) && nonFinite.confidence == 0.0);
    }

    {
        MgpuPlanner planner;
        MgpuInput m;
        m.available = true;
        m.stable = true;
        m.primaryNrMs = 5.2;
        m.secondaryNrMs = 1.8;
        m.uploadGbps = 20.0;
        m.downloadGbps = 20.0;
        m.renderWidth = 1920;
        m.renderHeight = 1080;
        m.workingScale = 0.58;
        const auto plan = planner.Plan(m);
        assert(plan.workWidth < m.renderWidth);
        assert(plan.workHeight < m.renderHeight);
        assert(plan.residualOnlyReturn);
        assert(plan.estimatedTransferMs > 0.0);
        assert(plan.useSecondary);

        m.workingScale = std::numeric_limits<double>::quiet_NaN();
        m.uploadGbps = std::numeric_limits<double>::quiet_NaN();
        const auto invalid = planner.Plan(m);
        assert(std::isfinite(invalid.estimatedTransferMs));
        assert(!invalid.useSecondary);

        m.renderWidth = 0;
        const auto empty = planner.Plan(m);
        assert(empty.workWidth == 0 && empty.workHeight == 0 && !empty.useSecondary);

        m.renderWidth = std::numeric_limits<std::uint32_t>::max();
        m.renderHeight = std::numeric_limits<std::uint32_t>::max();
        m.workingScale = 1.0;
        m.uploadGbps = 1.0;
        m.downloadGbps = 1.0;
        m.bytesPerInputPixel = std::numeric_limits<std::uint32_t>::max();
        m.bytesPerResidualPixel = std::numeric_limits<std::uint32_t>::max();
        const auto saturated = planner.Plan(m);
        assert(std::isfinite(saturated.estimatedTransferMs));
        assert(saturated.estimatedTransferMs >= 1e9);
        assert(!saturated.useSecondary);

        m.renderWidth = 1920;
        m.renderHeight = 1080;
        m.bytesPerInputPixel = 16;
        m.bytesPerResidualPixel = 8;
        m.uploadGbps = std::numeric_limits<double>::denorm_min();
        m.downloadGbps = std::numeric_limits<double>::denorm_min();
        const auto tinyBandwidth = planner.Plan(m);
        assert(std::isfinite(tinyBandwidth.estimatedTransferMs));
        assert(std::isfinite(tinyBandwidth.estimatedCriticalMs));
        assert(std::isfinite(tinyBandwidth.estimatedGainMs));
        assert(!tinyBandwidth.useSecondary);
    }


    {
        PerformanceConfig cfg;
        cfg.minScale = 0.5f;
        cfg.maxScale = 1.0f;
        cfg.scaleSteps = {2.0f, 1.0f, 0.85f, 0.5f, -1.0f, std::nanf("")};
        PerformanceController c(cfg);
        assert(c.Config().scaleSteps.size() == 3);
        assert(c.Config().scaleSteps.front() == 1.0f);
        assert(c.Config().scaleSteps.back() == 0.5f);
    }

    {
        // Automatic budget follows frame target: at 120 FPS and 30%, budget is about 2.5 ms.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.automaticNrBudget = true;
        cfg.nrBudgetFrameFraction = 0.30;
        PerformanceController c(cfg);
        const auto d = c.Update(sample(2.0, 7.0, 120, 120, 0.0));
        assert(d.resolvedNrBudgetMs > 2.4 && d.resolvedNrBudgetMs < 2.6);
    }

    {
        // Predictive scale-down should be able to skip a rung instead of forcing repeated rebuilds.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.automaticNrBudget = false;
        cfg.nrBudgetMs = 2.0;
        cfg.scaleDownSustainSeconds = 0.10;
        cfg.cooldownSeconds = 10.0;
        cfg.maxPredictiveStepDrop = 2;
        PerformanceController c(cfg);
        c.Reset(1.0f);
        for (int i = 0; i < 20; ++i) c.Update(sample(7.0, 12.0, 90, 90, 0.0, 1.0/60.0));
        assert(c.WorkingScale() <= 0.75f + 0.001f);
    }

    {
        NvofPolicy p;
        const auto autoPlan = p.Resolve(3840, 2160, NvofResolution::Auto);
        assert(autoPlan.selected == NvofResolution::P180);
        assert(autoPlan.height == 180);
        assert(autoPlan.width == 320);
        assert(std::fabs(autoPlan.motionScaleX - 12.0) < 0.001);
        assert(std::fabs(autoPlan.motionScaleY - 12.0) < 0.001);
        const auto qualityPlan = p.Resolve(2560, 1440, NvofResolution::P720);
        assert(qualityPlan.height == 720);
        assert(qualityPlan.width == 1280);
    }


    {
        // A reduced work-size build failure is quarantined and falls upward instead of retrying forever.
        PerformanceConfig cfg;
        cfg.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f};
        cfg.minScale = 0.67f;
        PerformanceController c(cfg);
        c.Reset(0.85f);
        const auto fallback = c.ReportScaleBuildFailure(0.85f);
        assert(fallback && std::fabs(*fallback - 1.0f) < 0.001f);
        assert(c.IsScaleBuildFailed(0.85f));
        c.Reset(0.85f);
        assert(std::fabs(c.WorkingScale() - 1.0f) < 0.001f);
        c.ClearScaleBuildFailures();
        c.Reset(0.85f);
        assert(std::fabs(c.WorkingScale() - 0.85f) < 0.001f);
    }

    {
        // Predictive downshift must skip a fully quarantined local window and find the next
        // usable rung instead of reselecting a known-bad scale.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        cfg.automaticNrBudget = false;
        cfg.nrBudgetMs = 2.0;
        cfg.scaleDownSustainSeconds = 0.01;
        cfg.cooldownSeconds = 0.0;
        cfg.maxPredictiveStepDrop = 2;
        cfg.scaleSteps = {1.0f, 0.85f, 0.75f, 0.67f, 0.58f};
        cfg.minScale = 0.58f;
        PerformanceController c(cfg);
        c.Reset(1.0f);
        assert(c.ReportScaleBuildFailure(0.85f));
        c.Reset(1.0f);
        assert(c.ReportScaleBuildFailure(0.75f));
        c.Reset(1.0f);
        bool changed = false;
        for (int i = 0; i < 8 && !changed; ++i)
            changed = c.Update(sample(8.0, 12.0, 120, 120, 0.0, 0.01)).changedScale;
        assert(changed && std::fabs(c.WorkingScale() - 0.67f) < 0.001f);
    }

    {
        // Missing NR GPU timing after a rebuild is unknown, not permission to scale up.
        PerformanceConfig cfg;
        cfg.targetFps = 60.0;
        cfg.scaleUpSustainSeconds = 0.05;
        cfg.cooldownSeconds = 0.0;
        PerformanceController c(cfg);
        c.Reset(0.67f);
        for (int i = 0; i < 120; ++i) c.Update(sample(0.0, 5.0, 60, 60, 0.0));
        assert(std::fabs(c.WorkingScale() - 0.67f) < 0.001f);
    }

    {
        TelemetryTracker tracker({0.20, 3});
        tracker.OnSourceFrame(0.000);
        tracker.OnNrSubmitted();
        tracker.OnSourceFrame(0.010);
        tracker.OnNrSubmitted();
        tracker.OnNrCompleted(0.012);
        tracker.OnSourceFrame(0.020);
        tracker.OnNrSubmitted();
        const auto t = tracker.BuildSample(0.010, 2.4, 8.0, 0.25);
        assert(t.sourceFps > 90.0);
        assert(t.processedFps == 0.0); // only one completion: no interval yet
        assert(t.queuePressure > 0.60 && t.queuePressure < 0.70);
        tracker.OnNrCompleted(0.022);
        const auto t2 = tracker.BuildSample(0.010, 2.4, 8.0);
        assert(t2.processedFps > 90.0);
        assert(t2.queuePressure > 0.30 && t2.queuePressure < 0.34);
    }

    {
        MotionGuideValidator validator;
        MotionGuideSample good;
        good.present = true;
        good.validPixelRatio = 0.98;
        good.temporalAgreement = 0.95;
        good.depthAgreement = 0.96;
        good.magnitudeSanity = 0.99;
        assert(!validator.Update(good).reliable); // hysteresis: first good frame is not enough
        assert(validator.Update(good).reliable);

        MotionGuideSample bad = good;
        bad.temporalAgreement = 0.05;
        assert(validator.Update(bad).reliable); // first bad frame is held
        assert(!validator.Update(bad).reliable);

        const auto firstStatic = ResolveStaticMotion(true, false);
        assert(!firstStatic.zeroMotion && firstStatic.maskHistory);
        const auto confirmedStatic = ResolveStaticMotion(true, true);
        assert(confirmedStatic.zeroMotion && !confirmedStatic.maskHistory);
    }

    {
        MotionGuideValidationConfig cfg;
        cfg.enterReliable = std::numeric_limits<double>::quiet_NaN();
        cfg.exitReliable = std::numeric_limits<double>::quiet_NaN();
        MotionGuideValidator validator(cfg);
        MotionGuideSample sample;
        sample.present = true;
        sample.validPixelRatio = std::numeric_limits<double>::quiet_NaN();
        const auto r = validator.Update(sample);
        assert(!r.reliable);
        assert(std::isfinite(r.confidence));
    }

    {
        TemporalHistoryRegistry histories;
        ViewDescriptor left;
        left.featureKey = 1001;
        left.width = 960; left.height = 1080;
        left.outputWidth = 1920; left.outputHeight = 1080;
        ViewDescriptor right = left;
        right.featureKey = 1001; // same temporal feature can still expose independent sub-views
        right.viewKey = 2;
        left.viewKey = 1;
        right.x = 960;

        const auto a = histories.Acquire(left, 1);
        const auto b = histories.Acquire(right, 1);
        assert(a.resetRequired && b.resetRequired);
        assert(a.historyId != b.historyId);
        const auto a2 = histories.Acquire(left, 2);
        assert(!a2.resetRequired && a2.historyId == a.historyId);

        left.width = 900;
        const auto resized = histories.Acquire(left, 3);
        assert(resized.resetRequired && resized.historyId != a.historyId);
        histories.Prune(1000, 100);
        assert(histories.Size() == 0);
    }


    {
        PipelinedExecutorState pipeline(2);
        assert(!pipeline.TrySubmit(101, 0, 7));
        const auto f1 = pipeline.TrySubmit(101, 1, 7);
        const auto f2 = pipeline.TrySubmit(102, 2, 7);
        assert(f1 && f2);
        assert(!pipeline.TrySubmit(103, 3, 7));
        assert(std::fabs(pipeline.QueuePressure() - 1.0) < 0.001);
        assert(pipeline.Complete(*f1));
        assert(!pipeline.ConsumeLatestBefore(1, 7)); // never consume same-frame output
        const auto ready1 = pipeline.ConsumeLatestBefore(2, 7);
        assert(ready1 && ready1->frameId == 1 && ready1->workId == 101);
        const auto f3 = pipeline.TrySubmit(103, 3, 7);
        assert(f3);

        const auto stale = *f2;
        pipeline.Reset();
        assert(!pipeline.Complete(stale)); // stale completion from an old helper/session is ignored
        assert(pipeline.Outstanding() == 0);

        const auto r1 = pipeline.TrySubmit(110, 10, 7);
        assert(r1);
        pipeline.RequestReconfigure();
        assert(!pipeline.TrySubmit(111, 11, 7)); // freeze admission while old resources drain
        assert(!pipeline.ApplyReconfigureIfIdle());
        assert(pipeline.Complete(*r1));
        assert(pipeline.DiscardReadyForReconfigure() == 1);
        const auto oldGeneration = pipeline.Generation();
        assert(pipeline.ApplyReconfigureIfIdle());
        assert(pipeline.Generation() != oldGeneration);
        assert(pipeline.TrySubmit(111, 11, 7));
    }

    {
        // Work IDs are exact, runtime-instance-unique and session-safe. Two views can share one source frame.
        WorkLedger works;
        bool invalidWorkRejected = false;
        try { (void)works.Begin(0, 1); } catch (const std::invalid_argument&) { invalidWorkRejected = true; }
        assert(invalidWorkRejected);
        invalidWorkRejected = false;
        try { (void)works.Begin(50, 1, 0, std::numeric_limits<float>::quiet_NaN()); }
        catch (const std::invalid_argument&) { invalidWorkRejected = true; }
        assert(invalidWorkRejected);

        const auto left = works.Begin(50, 1);
        const auto right = works.Begin(50, 2);
        assert(left.id != right.id && left.sourceFrame == right.sourceFrame);
        assert(works.Submit(left) && works.Submit(right));
        auto forged = left;
        forged.configurationGeneration += 1;
        assert(!works.IsSubmitted(forged));
        assert(!works.Complete(forged));
        assert(works.IsSubmitted(left));
        assert(works.Complete(right)); // out-of-order completion is exact
        const auto old = left;
        works.ResetSession();
        const auto next = works.Begin(51, 1);
        assert(next.id != old.id && next.session != old.session);
        assert(!works.Complete(old));
        assert(works.Abandon(next));
        assert(works.Outstanding() == 0);

        // Bursts above the reserved common case still preserve exact out-of-order identity.
        std::vector<WorkTicket> burst;
        burst.reserve(64);
        for (std::uint64_t i = 0; i < 64; ++i) {
            auto ticket = works.Begin(100 + i, i % 4);
            assert(works.Submit(ticket));
            burst.push_back(ticket);
        }
        assert(works.Outstanding() == burst.size());
        for (auto it = burst.rbegin(); it != burst.rend(); ++it)
            assert(works.Complete(*it));
        assert(works.Outstanding() == 0);
    }

    {
        // Timing FIFO can represent an invalid timed attempt without completing the next workload.
        TimingWorkMapper mapper(2);
        WorkLedger works;
        const auto a = works.Begin(1); works.Submit(a);
        const auto b = works.Begin(2); works.Submit(b);
        assert(!mapper.Push(a));
        assert(!mapper.PushInvalid());
        const auto dropped = mapper.Push(b); // bounded: oldest mapped work is surfaced to caller
        assert(dropped && dropped->id == a.id);
        const auto first = mapper.Pop();
        assert(first && !first->mapsWork);
        const auto second = mapper.Pop();
        assert(second && second->mapsWork && second->ticket.id == b.id);
    }

    {
        // Slot-addressed timing cannot confuse Vulkan query-ring order with FIFO workload order.
        TimingSlotMapper slots(4);
        WorkLedger works;
        const auto a = works.Begin(10, 1); works.Submit(a);
        const auto b = works.Begin(11, 2); works.Submit(b);
        const auto assignedA = slots.Assign(3, a);
        const auto assignedB = slots.Assign(1, b);
        assert(assignedA.accepted && !assignedA.displaced);
        assert(assignedB.accepted && !assignedB.displaced);
        const auto invalid = slots.Assign(4, a);
        assert(!invalid.accepted && !invalid.displaced);
        const auto rb = slots.Take(1);
        assert(rb && rb->id == b.id);
        const auto ra = slots.Take(3);
        assert(ra && ra->id == a.id);
        const auto c = works.Begin(12); works.Submit(c);
        const auto d = works.Begin(13); works.Submit(d);
        const auto assignedC = slots.Assign(0, c);
        assert(assignedC.accepted && !assignedC.displaced);
        const auto displaced = slots.Assign(0, d);
        assert(displaced.accepted && displaced.displaced && displaced.displaced->id == c.id);
        assert(slots.Clear(0)->id == d.id);
    }

    {
        // Pipeline results are isolated by view and stale ready frames are reclaimed automatically.
        PipelinedExecutorState pipeline(4);
        auto l1 = pipeline.TrySubmit(1, 10, 1);
        auto r1 = pipeline.TrySubmit(2, 10, 2);
        auto l2 = pipeline.TrySubmit(3, 11, 1);
        assert(l1 && r1 && l2);
        assert(pipeline.Complete(*l1) && pipeline.Complete(*r1) && pipeline.Complete(*l2));
        const auto left = pipeline.ConsumeLatestBefore(12, 1);
        assert(left && left->workId == 3);
        assert(pipeline.Outstanding() == 1); // left frame 10 was discarded; right remains
        const auto right = pipeline.ConsumeLatestBefore(12, 2);
        assert(right && right->workId == 2 && pipeline.Outstanding() == 0);
    }

    {
        // Simultaneous multi-view workloads count as multiple events once there is a time baseline.
        TelemetryTracker tracker({0.50, 0.75, 4});
        tracker.OnSourceWork(0.0);
        tracker.OnSourceWork(0.0);
        tracker.OnSourceWork(0.01);
        tracker.OnSourceWork(0.01);
        const auto s = tracker.BuildSample(0.01, 2.0, 8.0, 0.0, 0.0, 0.0, false, false, false, 0.01);
        assert(s.sourceFps > 190.0 && s.sourceFps < 210.0); // 2 views / 10 ms = 200 workloads/s
        tracker.OnNrSubmitted();
        tracker.OnNrCompleted(0.0); // throughput time anchor
        tracker.OnNrSubmitted(); tracker.OnNrSubmitted();
        tracker.OnNrCompleted(0.01); tracker.OnNrCompleted(0.01);
        const auto good = tracker.BuildSample(0.01, 2.0, 8.0, 0.0, 0.0, 0.0, false, false, false, 0.01);
        assert(good.processedFps > 190.0 && good.processedFps < 210.0);
        const auto stale = tracker.BuildSample(0.01, 2.0, 8.0, 0.0, 0.0, 0.0, false, false, false, 2.0);
        assert(stale.processedFps < good.processedFps * 0.1);

        TelemetryTracker finiteTracker({std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity(), 0});
        const auto finite = finiteTracker.BuildSample(
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), false, false, false, 0.0);
        assert(std::isfinite(finite.dtSeconds) && finite.dtSeconds == 0.0);
        assert(std::isfinite(finite.nrGpuMs) && finite.nrGpuMs == 0.0);
        assert(std::isfinite(finite.frameGpuMs) && finite.frameGpuMs == 0.0);
        assert(std::isfinite(finite.asyncOverlap) && finite.asyncOverlap == 0.0);
        assert(std::isfinite(finite.crossAdapterMs) && finite.crossAdapterMs == 0.0);
        assert(std::isfinite(finite.secondaryNrGpuMs) && finite.secondaryNrGpuMs == 0.0);

        // View count may change dynamically. Throughput belongs to the workloads AFTER the anchor:
        // one view at t=0 followed by two at t=10 ms is 200 workloads/s, not 100.
        TelemetryTracker variableViews({0.50, 0.75, 4});
        variableViews.OnSourceWork(0.0);
        variableViews.OnSourceWork(0.01);
        variableViews.OnSourceWork(0.01);
        const auto expanded = variableViews.BuildSample(0.01, 2.0, 8.0, 0.0, 0.0, 0.0, false, false, false, 0.01);
        assert(expanded.sourceFps > 190.0 && expanded.sourceFps < 210.0);

        // And the inverse transition must drop immediately rather than counting the old two-view burst.
        TelemetryTracker reducedViews({0.50, 0.75, 4});
        reducedViews.OnSourceWork(0.0);
        reducedViews.OnSourceWork(0.0);
        reducedViews.OnSourceWork(0.01);
        const auto reduced = reducedViews.BuildSample(0.01, 2.0, 8.0, 0.0, 0.0, 0.0, false, false, false, 0.01);
        assert(reduced.sourceFps > 90.0 && reduced.sourceFps < 110.0);
    }

    {
        // Overlap uses union coverage, so overlapping graphics intervals never double-count.
        const GpuInterval nr{10.0, 20.0};
        const std::vector<GpuInterval> concurrent{{8.0, 15.0}, {12.0, 18.0}, {19.0, 25.0}};
        const double overlap = AsyncOverlapEstimator::Instantaneous(nr, concurrent);
        assert(std::fabs(overlap - 0.9) < 0.001); // [10,18] + [19,20]

        AsyncOverlapEstimator finiteSmoothing(std::numeric_limits<double>::infinity());
        assert(finiteSmoothing.Update(nr, {}, 1.0) == 0.0);
        const double recovered = finiteSmoothing.Update(nr, {{10.0, 20.0}}, 1.0);
        assert(std::isfinite(recovered) && recovered > 0.5);

        // More than the inline interval budget still preserves exact union coverage.
        const std::vector<GpuInterval> manyIntervals(32, GpuInterval{10.0, 20.0});
        assert(std::fabs(AsyncOverlapEstimator::Instantaneous(nr, manyIntervals) - 1.0) < 0.001);
    }


    {
        // Auto mode should qualify async only when it improves real frame time and tail latency.
        AsyncQualificationConfig cfg;
        cfg.baselineSamples = 8;
        cfg.trialSamples = 8;
        cfg.minMedianGain = 0.03;
        cfg.maxP95Regression = 0.05;
        cfg.minMeanOverlap = 0.10;
        AsyncQualification q(cfg);
        assert(q.NextMode(true) == SchedulerMode::Serialized);
        TelemetrySample serial = sample(4.0, 10.0, 120.0, 120.0, 0.1);
        for (int i = 0; i < 8; ++i) q.Observe(SchedulerMode::Serialized, serial);
        assert(q.ReadyForTrial());
        assert(q.NextMode(true) == SchedulerMode::AsyncCompute);
        TelemetrySample async = sample(4.0, 8.8, 120.0, 120.0, 0.2);
        async.asyncOverlap = 0.30;
        for (int i = 0; i < 8; ++i) q.Observe(SchedulerMode::AsyncCompute, async);
        const auto result = q.Result();
        assert(result.qualified && result.medianGain > 0.10);
        assert(result.p95Regression < 0.0);
        assert(q.NextMode(true) == SchedulerMode::AsyncCompute);
        assert(q.NextMode(false) == SchedulerMode::Serialized);
    }

    {
        // Overlap alone is not enough: async that worsens frame pacing is rejected automatically.
        AsyncQualificationConfig cfg;
        cfg.baselineSamples = 6;
        cfg.trialSamples = 6;
        AsyncQualification q(cfg);
        TelemetrySample serial = sample(4.0, 10.0, 120.0, 120.0, 0.1);
        for (int i = 0; i < 6; ++i) q.Observe(SchedulerMode::Serialized, serial);
        TelemetrySample async = sample(4.0, 10.4, 120.0, 120.0, 0.2);
        async.asyncOverlap = 0.45;
        for (int i = 0; i < 6; ++i) q.Observe(SchedulerMode::AsyncCompute, async);
        assert(q.State() == AsyncQualificationState::PreferSerialized);
        assert(!q.Result().qualified);
        assert(q.NextMode(true) == SchedulerMode::Serialized);
    }

    {
        // Invalid async telemetry/config must not poison or prematurely advance the qualifier.
        AsyncQualificationConfig cfg;
        cfg.baselineSamples = 5;
        cfg.trialSamples = 5;
        cfg.minMedianGain = std::numeric_limits<double>::quiet_NaN();
        cfg.maxP95Regression = std::numeric_limits<double>::quiet_NaN();
        cfg.minMeanOverlap = std::numeric_limits<double>::quiet_NaN();
        cfg.maxMeanQueuePressure = std::numeric_limits<double>::quiet_NaN();
        AsyncQualification q(cfg);
        TelemetrySample s;
        s.frameGpuMs = 10.0;
        for (int i = 0; i < 5; ++i) q.Observe(SchedulerMode::Serialized, s);
        assert(q.State() == AsyncQualificationState::ReadyForTrial);

        s.frameGpuMs = 8.0;
        s.asyncOverlap = std::numeric_limits<double>::quiet_NaN();
        s.queuePressure = 0.1;
        q.Observe(SchedulerMode::AsyncCompute, s);
        assert(q.State() == AsyncQualificationState::ReadyForTrial);
        s.asyncOverlap = 0.4;
        s.queuePressure = std::numeric_limits<double>::quiet_NaN();
        q.Observe(SchedulerMode::AsyncCompute, s);
        assert(q.State() == AsyncQualificationState::ReadyForTrial);

        s.queuePressure = 0.1;
        for (int i = 0; i < 5; ++i) q.Observe(SchedulerMode::AsyncCompute, s);
        const auto r = q.Result();
        assert(r.state == AsyncQualificationState::PreferAsync);
        assert(std::isfinite(r.meanAsyncOverlap) && std::isfinite(r.meanAsyncQueuePressure));
    }

    {
        // Manual displayed-FPS cap and NRFusion real/source cap are resolved in one domain.
        auto p = FrameLimitPolicy::Resolve(120.0, 75.0, true);
        assert(std::fabs(p.manualSourceCapFps - 60.0) < 0.001);
        assert(std::fabs(p.sourceCapFps - 60.0) < 0.001 && !p.governorActive);
        p = FrameLimitPolicy::Resolve(0.0, 75.0, true);
        assert(std::fabs(p.sourceCapFps - 75.0) < 0.001 && p.governorActive);

        // MFG: infer displayed/source multiplier from the two host timing domains.
        assert(FrameLimitPolicy::EstimateGenerationMultiplier(16.666, 8.333, true) == 2.0);
        assert(FrameLimitPolicy::EstimateGenerationMultiplier(16.666, 5.555, true) == 3.0);
        assert(FrameLimitPolicy::EstimateGenerationMultiplier(16.666, 4.166, true) == 4.0);
        assert(FrameLimitPolicy::EstimateGenerationMultiplier(16.666, 2.777, true) == 4.0);
        assert(FrameLimitPolicy::EstimateGenerationMultiplier(0.0, 0.0, true) == 2.0);
        assert(FrameLimitPolicy::EstimateGenerationMultiplier(16.666, 4.166, false) == 1.0);
        p = FrameLimitPolicy::Resolve(240.0, 80.0, 99.0);
        assert(std::fabs(p.manualSourceCapFps - 60.0) < 0.001);
        assert(std::fabs(p.sourceCapFps - 60.0) < 0.001);
        assert(std::fabs(p.generationMultiplier - 4.0) < 0.001);
    }

    {
        GenerationMultiplierTracker tracker(3);
        assert(tracker.Update(16.666, 5.555, true) == 3.0);
        assert(tracker.Update(16.666, 4.166, true) == 3.0);
        assert(tracker.Update(16.666, 5.555, true) == 3.0);
        assert(tracker.Update(16.666, 4.166, true) == 3.0);
        assert(tracker.Update(16.666, 4.166, true) == 3.0);
        assert(tracker.Update(16.666, 4.166, true) == 4.0);
        assert(tracker.Update(0.0, 0.0, true) == 4.0);
        assert(tracker.Update(16.666, 8.333, false) == 1.0);
    }

    {
        // Residual history is accepted only when motion/depth/confidence agree.
        ResidualReprojection repro;
        ResidualReprojectionInput in;
        in.current.width = in.history.width = 2;
        in.current.height = in.history.height = 1;
        in.current.residual = {1.0f, 1.0f};
        in.history.residual = {3.0f, 5.0f};
        in.current.preExposure = in.history.preExposure = 1.0f;
        in.current.sourceFrame = 2;
        in.history.sourceFrame = 1;
        in.current.depth = in.history.depth = {10.0f, 10.0f};
        in.motionX = {0.0f, 0.0f}; in.motionY = {0.0f, 0.0f}; in.confidence = {1.0f, 1.0f};
        auto out = repro.Run(in);
        assert(out.historyAccepted[0] && out.historyAccepted[1]);
        assert(out.image.residual[0] > 2.7f && out.image.residual[1] > 4.6f);
        in.history.preExposure = 2.0f;
        out = repro.Run(in);
        assert(!out.historyAccepted[0] && !out.historyAccepted[1]);
        in.history.preExposure = 1.0f;
        in.current.depth[1] = 50.0f;
        out = repro.Run(in);
        assert(out.historyAccepted[0] && !out.historyAccepted[1]);
        assert(std::fabs(out.image.residual[1] - 1.0f) < 0.001f);

        // Non-finite confidence/current residual must never poison the temporal chain with NaNs.
        in.current.depth = {10.0f, 10.0f};
        in.current.residual = {std::numeric_limits<float>::quiet_NaN(), 1.0f};
        in.confidence = {std::numeric_limits<float>::quiet_NaN(), 1.0f};
        out = repro.Run(in);
        assert(!out.historyAccepted[0]);
        assert(std::isfinite(out.image.residual[0]) && out.image.residual[0] == 0.0f);
        assert(std::isfinite(out.image.residual[1]));

        ResidualReprojection sanitized({std::numeric_limits<float>::quiet_NaN(), 2.0f, -1.0f, -4.0f});
        out = sanitized.Run(in);
        for (float v : out.image.residual) assert(std::isfinite(v));

        // On the 32-bit carrier, a hostile/impossible shape must fail closed before width*height
        // wraps size_t and turns the following loops/indexing into out-of-bounds access.
        if constexpr (sizeof(std::size_t) == 4) {
            ResidualReprojectionInput oversized;
            oversized.current.width = std::numeric_limits<std::uint32_t>::max();
            oversized.current.height = 2;
            const auto rejected = repro.Run(oversized);
            assert(rejected.image.residual.empty() && rejected.historyAccepted.empty());
        }
    }

    {
        // Adapter event path uses exact WorkIds and timing FIFO; an invalid interval cannot complete work.
        auto& adapter = OptiScalerAdapter::Instance();
        adapter.Reset(1.0f);
        const auto a = adapter.BeginNrWork(1, 1);
        assert(adapter.SubmitNrWork(a));
        assert(adapter.MapTimedWork(a));
        const auto b = adapter.BeginNrWork(1, 2);
        assert(adapter.SubmitNrWork(b));
        auto forgedB = b;
        forgedB.precisionTag = 4;
        assert(!adapter.MapTimedWork(forgedB));
        adapter.MapInvalidTimedAttempt();
        assert(adapter.RetireTimedInterval(2.5));
        assert(!adapter.RetireTimedInterval(2.5));
        assert(adapter.TrackedSourceFps() >= 0.0 && adapter.TrackedProcessedFps() >= 0.0);
        assert(adapter.AbandonNrWork(b));
        adapter.Reset(1.0f);
    }

    {
        // The adapter and FusionRuntime share one adaptive controller. A central Auto shape change
        // must advance the adapter work generation so a timing from the previous shape cannot train
        // the new decision epoch.
        auto& adapter = OptiScalerAdapter::Instance();
        adapter.Reset(1.0f);
        AdaptiveSettings settings;
        settings.enabled = true;
        settings.mode = UserMode::Auto;
        settings.targetFps = 120.0;

        GameContext game;
        game.api = GraphicsApi::D3D12;
        game.nativeDlss = true;
        FrameContext frame;
        frame.frameId = 1;
        frame.api = GraphicsApi::D3D12;
        frame.color = {1, {1920, 1080}, ResourceFormat::Rgba16Float};
        frame.renderResolution = {1920, 1080};
        frame.outputResolution = {1920, 1080};
        RuntimeCapabilities caps;
        caps.nativeProvider = true;
        caps.preSr = true;
        caps.fp8 = true;

        auto first = adapter.ResolveAuto(game, frame, caps, settings, 1.0, 7.0, 120.0, 120.0);
        assert(first.supported);
        const auto oldGeneration = adapter.ScaleGeneration();
        const auto oldWork = adapter.BeginNrWork(1, 0, first.workingScale);
        assert(adapter.SubmitNrWork(oldWork));
        assert(adapter.MapTimedWork(oldWork));

        frame.frameId = 2;
        frame.color.resolution = {1280, 720};
        frame.renderResolution = {1280, 720};
        const auto second = adapter.ResolveAuto(game, frame, caps, settings, 1.0, 7.0, 120.0, 120.0);
        assert(second.supported);
        assert(adapter.ScaleGeneration() != oldGeneration);
        assert(adapter.RetireTimedInterval(2.0));
        assert(adapter.TrackedNrGpuMs() == 0.0);
        adapter.Reset(1.0f);
    }

    {
        // Delayed GPU timings from an old WorkingScale generation may retire throughput, but must
        // never drive the controller after the scale changed.
        auto& adapter = OptiScalerAdapter::Instance();
        AdaptiveSettings settings;
        settings.enabled = true;
        settings.mode = UserMode::MaxFps;
        settings.targetFps = 120.0;
        adapter.ResetForShape(settings);
        const auto oldGeneration = adapter.ScaleGeneration();
        const auto oldWork = adapter.BeginNrWork(10, 0);
        assert(oldWork.configurationGeneration == oldGeneration);
        assert(adapter.SubmitNrWork(oldWork));
        assert(adapter.MapTimedWork(oldWork));
        const auto fallback = adapter.ReportWorkingScaleBuildFailure(0.75f);
        assert(fallback && *fallback > 0.75f);
        assert(adapter.ScaleGeneration() != oldGeneration);
        assert(adapter.RetireTimedInterval(9.0));
        assert(adapter.TrackedNrGpuMs() == 0.0);

        const auto currentWork = adapter.BeginNrWork(11, 0);
        assert(currentWork.configurationGeneration == adapter.ScaleGeneration());
        assert(adapter.SubmitNrWork(currentWork));
        assert(adapter.MapTimedWork(currentWork));
        assert(adapter.RetireTimedInterval(2.0));
        assert(std::fabs(adapter.TrackedNrGpuMs() - 2.0) < 0.001);
        adapter.Reset(1.0f);
    }

    {
        // FusionRuntime now actually exposes the policies it owns instead of leaving them disconnected.
        FusionRuntime runtime;
        const auto nvof = runtime.ResolveNvof(3840, 2160);
        assert(nvof.height == 180);
        MotionGuideSample guide; guide.present = true;
        guide.validPixelRatio = guide.temporalAgreement = guide.depthAgreement = guide.magnitudeSanity = 1.0;
        runtime.UpdateMotionGuide(guide);
        const auto guide2 = runtime.UpdateMotionGuide(guide);
        assert(guide2.reliable);
        ViewDescriptor view; view.featureKey = 77; view.width = 1920; view.height = 1080;
        assert(runtime.AcquireHistory(view, 1).resetRequired);
        assert(!runtime.AcquireHistory(view, 2).resetRequired);
    }


    {
        // ResolvePrecision is a hot runtime path. It must be callable repeatedly without recursive
        // locking/deadlock, including when the candidate is unavailable.
        auto& adapter = OptiScalerAdapter::Instance();
        adapter.Reset(1.0f);
        for (int i = 0; i < 64; ++i) {
            assert(adapter.ResolvePrecision(true, false) == NrPrecision::Fp8);
            assert(adapter.ResolvePrecision(false, true, NrPrecision::HybridNvfp4) ==
                   NrPrecision::HybridNvfp4);
        }
        adapter.Reset(1.0f);
    }

    {
        // Production precision autotuner: drain baseline, switch candidate, and keep NVFP4 only
        // when both median and tail of the whole NR pass improve.
        PrecisionAutotuneConfig cfg;
        cfg.warmupSamples = 2;
        cfg.measureSamples = 6;
        cfg.minMedianGain = 0.02;
        cfg.minAbsoluteGainMs = 0.05;
        cfg.maxP95Regression = 0.02;
        PrecisionAutotuner tuner(cfg);
        tuner.Reset(7);
        assert(tuner.Desired(true, true) == NrPrecision::Fp8);
        for (int i = 0; i < 2; ++i) tuner.Observe(NrPrecision::Fp8, 7, 4.0);
        for (int i = 0; i < 6; ++i) tuner.Observe(NrPrecision::Fp8, 7, 4.0 + (i % 2) * 0.04);
        assert(tuner.Desired(true, true) == NrPrecision::HybridNvfp4);
        // Delayed FP8 timings after the switch must not advance candidate warmup/measurement.
        for (int i = 0; i < 4; ++i) tuner.Observe(NrPrecision::Fp8, 7, 4.0);
        for (int i = 0; i < 2; ++i) tuner.Observe(NrPrecision::HybridNvfp4, 7, 3.4);
        for (int i = 0; i < 6; ++i) tuner.Observe(NrPrecision::HybridNvfp4, 7, 3.4 + (i % 2) * 0.03);
        const auto r = tuner.Result();
        assert(r.finished && r.candidateQualified && r.medianGain > 0.10);
        assert(tuner.Desired(true, true) == NrPrecision::HybridNvfp4);
        tuner.ReportCandidateFailure(7);
        assert(tuner.Desired(true, true) == NrPrecision::Fp8);
        assert(tuner.State() == PrecisionAutotuneState::CandidateFailed);
        // A new configuration generation is a fresh benchmark; manual/Custom always remains manual.
        tuner.Reset(8);
        assert(tuner.Desired(false, true, NrPrecision::HybridNvfp4) == NrPrecision::HybridNvfp4);
        assert(tuner.Desired(true, false) == NrPrecision::Fp8);
    }

    {
        // A tiny median win with worse tail is noise, not a reason to ship NVFP4 automatically.
        PrecisionAutotuneConfig cfg;
        cfg.warmupSamples = 1;
        cfg.measureSamples = 5;
        cfg.minMedianGain = 0.01;
        cfg.minAbsoluteGainMs = 0.02;
        cfg.maxP95Regression = 0.01;
        PrecisionAutotuner tuner(cfg);
        tuner.Reset(9);
        tuner.Observe(NrPrecision::Fp8, 9, 4.0);
        for (double v : {4.0, 4.0, 4.0, 4.0, 4.0}) tuner.Observe(NrPrecision::Fp8, 9, v);
        tuner.Observe(NrPrecision::HybridNvfp4, 9, 3.9);
        for (double v : {3.9, 3.9, 3.9, 3.9, 4.5}) tuner.Observe(NrPrecision::HybridNvfp4, 9, v);
        assert(tuner.Result().finished && !tuner.Result().candidateQualified);
        assert(tuner.Desired(true, true) == NrPrecision::Fp8);
    }

    {
        // NaN autotune thresholds must fall back to production defaults instead of poisoning the
        // final comparisons and permanently rejecting an otherwise clear NVFP4 win.
        PrecisionAutotuneConfig cfg;
        cfg.warmupSamples = 1;
        cfg.measureSamples = 5;
        cfg.minMedianGain = std::numeric_limits<double>::quiet_NaN();
        cfg.minAbsoluteGainMs = std::numeric_limits<double>::quiet_NaN();
        cfg.maxP95Regression = std::numeric_limits<double>::quiet_NaN();
        PrecisionAutotuner tuner(cfg);
        tuner.Reset(10);
        tuner.Observe(NrPrecision::Fp8, 10, 4.0);
        for (int i = 0; i < 5; ++i) tuner.Observe(NrPrecision::Fp8, 10, 4.0);
        tuner.Observe(NrPrecision::HybridNvfp4, 10, 3.0);
        for (int i = 0; i < 5; ++i) tuner.Observe(NrPrecision::HybridNvfp4, 10, 3.0);
        assert(tuner.Result().finished && tuner.Result().candidateQualified);
    }

    {
        // Custom is manual at the adapter boundary itself, not only because the generated host
        // currently sets enabled=false. Non-finite settings are normalized once and stay stable.
        auto& adapter = OptiScalerAdapter::Instance();
        adapter.Reset(1.0f);
        AdaptiveSettings custom;
        custom.enabled = true;
        custom.mode = UserMode::Custom;
        custom.fixedScale = 0.58f;
        custom.targetFps = 120.0;
        for (int i = 0; i < 32; ++i)
            assert(std::fabs(adapter.ResolveWorkingScale(custom, 50.0, 50.0, 240.0, 30.0, 1.0, 0.0) - 0.58f) < 1e-4f);

        custom.fixedScale = std::numeric_limits<float>::quiet_NaN();
        custom.targetFps = std::numeric_limits<double>::infinity();
        const float normalized = adapter.ResolveWorkingScale(custom, 0.0);
        const auto generation = adapter.ScaleGeneration();
        assert(std::isfinite(normalized) && std::fabs(normalized - 1.0f) < 1e-4f);
        assert(adapter.ResolveWorkingScale(custom, 0.0) == normalized);
        assert(adapter.ScaleGeneration() == generation);
        adapter.Reset(1.0f);
    }


    {
        // Precision qualification is cached per WorkingScale for the current base shape. A Target-FPS
        // change may rebuild controller state, but must not force another expensive FP8/NVFP4 trial.
        auto& a = OptiScalerAdapter::Instance();
        AdaptiveSettings ps;
        ps.enabled = true;
        ps.mode = UserMode::Auto;
        ps.targetFps = 120.0;
        a.ResetForShape(ps);
        const float scale = a.LastDecision().workingScale;
        std::uint64_t frame = 1;
        auto retire = [&](std::uint8_t precision, double ms) {
            const auto w = a.BeginNrWork(frame++, 0, scale, precision);
            assert(a.SubmitNrWork(w));
            assert(a.MapTimedWork(w));
            assert(a.RetireTimedInterval(ms));
        };
        for (int i = 0; i < 12 + 36; ++i) retire(0, 4.0);
        assert(a.ResolvePrecision(true, true) == NrPrecision::HybridNvfp4);
        for (int i = 0; i < 12 + 36; ++i) retire(4, 3.5);
        assert(a.PrecisionStatus().finished && a.PrecisionStatus().candidateQualified);
        assert(a.ResolvePrecision(true, true) == NrPrecision::HybridNvfp4);

        ps.targetFps = 100.0;
        (void)a.ResolveWorkingScale(ps, 0.0); // reconfigure controller/tuner, preserve same-shape cache
        assert(a.ResolvePrecision(true, true) == NrPrecision::HybridNvfp4);
    }

    {
        struct Fence {};
        struct Queue {
            std::vector<std::uint64_t> signals;
            std::vector<std::uint64_t> waits;
            long Signal(Fence*, std::uint64_t v) { signals.push_back(v); return 0; }
            long Wait(Fence*, std::uint64_t v) { waits.push_back(v); return 0; }
        } graphics, compute;
        Fence produced, completed;
        D3D12AsyncFenceSequencer seq;
        auto token = seq.QueueComputeAfterProducer(42, &graphics, &compute, &produced);
        assert(token && graphics.signals == std::vector<std::uint64_t>{1} &&
               compute.waits == std::vector<std::uint64_t>{1});
        assert(seq.SignalComputeComplete(*token, &compute, &completed));
        assert(compute.signals == std::vector<std::uint64_t>{1});
        assert(seq.QueueConsumerAfterCompute(*token, &graphics, &completed));
        assert(graphics.waits == std::vector<std::uint64_t>{1});

        // A neural-session reset may reuse the same D3D12 fence objects. Fence values must remain
        // monotonic or Wait(1) would already be satisfied by the old completed value.
        seq.Reset();
        auto token2 = seq.QueueComputeAfterProducer(43, &graphics, &compute, &produced);
        assert(token2 && token2->producerValue == 2);
        assert(seq.SignalComputeComplete(*token2, &compute, &completed));
        assert(token2->completionValue == 2);
    }

    {
        struct FakeD3D12Queue {
            std::uint64_t freq = 2'000'000;
            std::uint64_t gpu = 8'000'000;
            std::uint64_t cpu = 12'000'000;
            long GetTimestampFrequency(std::uint64_t* out) { *out = freq; return 0; }
            long GetClockCalibration(std::uint64_t* g, std::uint64_t* c) { *g = gpu; *c = cpu; return 0; }
        } q;
        CrossQueueClockCalibrator c;
        for (int i = 0; i < 3; ++i) {
            assert(UpdateD3D12QueueClock(c, &q, 1'000'000.0));
            q.gpu += q.freq; q.cpu += 1'000'000;
        }
        assert(c.Stable(D3D12QueueClockId(&q)));
    }

    {
        // Cross-queue GPU clocks with different frequencies/offsets are mapped into one QPC domain.
        CrossQueueClockCalibrator clocks({8, 3, 0.25, 0.001});
        for (std::uint64_t i = 1; i <= 3; ++i) {
            QueueClockCalibrationSample g;
            g.gpuTimestamp = i * 1'000'000ull;
            g.cpuQpcTimestamp = (10ull + i) * 1'000'000ull;
            g.gpuFrequencyHz = 1'000'000.0;
            g.cpuQpcFrequencyHz = 1'000'000.0;
            assert(clocks.Update(1, g));

            QueueClockCalibrationSample c;
            c.gpuTimestamp = i * 2'000'000ull;
            c.cpuQpcTimestamp = (9ull + i) * 1'000'000ull;
            c.gpuFrequencyHz = 2'000'000.0;
            c.cpuQpcFrequencyHz = 1'000'000.0;
            assert(clocks.Update(2, c));
        }
        assert(clocks.Stable(1) && clocks.Stable(2));

        // A queue/device recreation can reuse the same pointer/clock ID and frequency but start a
        // different timestamp epoch. A large calibration-offset jump must invalidate old samples.
        QueueClockCalibrationSample jumped;
        jumped.gpuTimestamp = 4'000'000ull;
        jumped.cpuQpcTimestamp = 14'050'000ull; // +50 ms discontinuity at unchanged frequencies
        jumped.gpuFrequencyHz = 1'000'000.0;
        jumped.cpuQpcFrequencyHz = 1'000'000.0;
        assert(clocks.Update(1, jumped));
        assert(!clocks.Stable(1));
        assert(clocks.Status(1).samples == 1);
        assert(!clocks.ToCommonSeconds(1, jumped.gpuTimestamp));

        // Rebuild a clean calibration window in the new epoch.
        for (std::uint64_t i = 1; i <= 2; ++i) {
            jumped.gpuTimestamp += 1'000'000ull;
            jumped.cpuQpcTimestamp += 1'000'000ull;
            assert(clocks.Update(1, jumped));
        }
        assert(clocks.Stable(1));

        // Rolling calibration keeps only the bounded window after wraparound.
        CrossQueueClockCalibrator rolling({4, 3, 0.25, 0.001});
        for (std::uint64_t i = 1; i <= 12; ++i) {
            QueueClockCalibrationSample sample;
            sample.gpuTimestamp = i * 1'000'000ull;
            sample.cpuQpcTimestamp = (10ull + i) * 1'000'000ull;
            sample.gpuFrequencyHz = 1'000'000.0;
            sample.cpuQpcFrequencyHz = 1'000'000.0;
            assert(rolling.Update(9, sample));
        }
        assert(rolling.Stable(9));
        assert(rolling.Status(9).samples == 4);
        const auto rollingMapped = rolling.ToCommonSeconds(9, 20'000'000ull);
        assert(rollingMapped && std::fabs(*rollingMapped - 30.0) < 1e-9);

        FusionRuntime runtime;
        runtime.QueueClocks() = clocks;
        const QueueGpuIntervalTicks nr{2, 10'004'000ull, 10'014'000ull}; // 14.002..14.007 s
        const std::vector<QueueGpuIntervalTicks> graphics{{1, 3'950'000ull, 3'956'000ull}}; // 14.000..14.006 s
        const auto overlap = runtime.ObserveCalibratedOverlap(nr, graphics, 1.0 / 60.0);
        assert(overlap && std::fabs(*overlap - 0.8) < 0.001);
    }

    {
        // The host adapter owns the bounded executor state: admission creates real queue pressure,
        // completion feeds processed throughput, results stay view-scoped, and reconfigure freezes
        // adaptive scale decisions until the old slots drain.
        auto& adapter = OptiScalerAdapter::Instance();
        adapter.Reset();
        const auto w1 = adapter.BeginNrWork(100, 1, 0.75f, 0);
        const auto w2 = adapter.BeginNrWork(100, 2, 0.75f, 0);
        const auto p1 = adapter.AdmitPipelinedWork(w1);
        const auto p2 = adapter.AdmitPipelinedWork(w2);
        assert(p1 && p2);
        assert(adapter.PipelineQueuePressure() > 0.65 && adapter.PipelineQueuePressure() < 0.68);
        auto forgedW1 = w1;
        forgedW1.workingScale = 0.58f;
        assert(!adapter.CompletePipelinedWork(*p1, forgedW1));
        assert(adapter.PipelineQueuePressure() > 0.65); // invalid metadata did not clear the slot
        assert(adapter.CompletePipelinedWork(*p1, w1));
        assert(adapter.CompletePipelinedWork(*p2, w2));
        const auto left = adapter.ConsumePipelinedBefore(101, 1);
        const auto right = adapter.ConsumePipelinedBefore(101, 2);
        assert(left && left->workId == w1.id);
        assert(right && right->workId == w2.id);
        assert(adapter.PipelineQueuePressure() == 0.0);

        const auto inFlight = adapter.BeginNrWork(101, 1, 0.75f, 0);
        const auto inFlightPipe = adapter.AdmitPipelinedWork(inFlight);
        assert(inFlightPipe);
        adapter.RequestPipelineReconfigure();
        assert(adapter.PipelineReconfigurePending());
        AdaptiveSettings autoSettings;
        autoSettings.mode = UserMode::Auto;
        const float frozen = adapter.ResolveWorkingScale(autoSettings, 20.0, 20.0, 240.0, 60.0, 1.0, 0.0);
        for (int i = 0; i < 20; ++i)
            assert(adapter.ResolveWorkingScale(autoSettings, 20.0, 20.0, 240.0, 60.0, 1.0, 0.0) == frozen);
        assert(!adapter.ApplyPipelineReconfigureIfIdle());
        assert(adapter.AbandonPipelinedWork(*inFlightPipe, inFlight));
        assert(adapter.ApplyPipelineReconfigureIfIdle());
        assert(!adapter.PipelineReconfigurePending());
    }

    {
        // Async qualification must never consume a stale overlap after a clock discontinuity.
        auto& adapter = OptiScalerAdapter::Instance();
        adapter.Reset();
        QueueClockCalibrationSample sample;
        sample.gpuFrequencyHz = 1'000'000.0;
        sample.cpuQpcFrequencyHz = 1'000'000.0;
        for (std::uint64_t i = 1; i <= 3; ++i) {
            sample.gpuTimestamp = i * 1'000'000ull;
            sample.cpuQpcTimestamp = (10ull + i) * 1'000'000ull;
            assert(adapter.UpdateQueueClock(101, sample));
            sample.gpuTimestamp = i * 1'000'000ull;
            sample.cpuQpcTimestamp = (20ull + i) * 1'000'000ull;
            assert(adapter.UpdateQueueClock(202, sample));
        }
        std::uint64_t sampleId = 1;
        for (int i = 0; i < 30; ++i)
            adapter.ObserveAsyncTrial(sampleId++, SchedulerMode::Serialized, 10.0, 0.1);
        assert(adapter.AsyncStatus().state == AsyncQualificationState::ReadyForTrial);
        const QueueGpuIntervalTicks nrTicks{202, 4'000'000ull, 4'008'000ull};
        const std::vector<QueueGpuIntervalTicks> gfxTicks{{101, 14'000'000ull, 14'006'000ull}};
        const std::uint64_t asyncSample = sampleId++;
        assert(adapter.ObserveAsyncOverlap(asyncSample, nrTicks, gfxTicks, 1.0 / 60.0));
        // A fresh overlap is valid only for the exact sample that produced it.
        adapter.ObserveAsyncTrial(asyncSample + 1, SchedulerMode::AsyncCompute, 1.0, 0.0);
        assert(adapter.AsyncStatus().state == AsyncQualificationState::ReadyForTrial);
        assert(adapter.ObserveAsyncOverlap(asyncSample, nrTicks, gfxTicks, 1.0 / 60.0));
        adapter.ObserveAsyncTrial(asyncSample, SchedulerMode::AsyncCompute, 9.0, 0.1);
        assert(adapter.AsyncStatus().state == AsyncQualificationState::CollectingAsyncTrial);

        // Same queue id/frequency, different epoch: qualifier resets. A trial frame without a new
        // calibrated overlap sample must then be ignored.
        sample.gpuTimestamp = 5'000'000ull;
        sample.cpuQpcTimestamp = 25'050'000ull;
        assert(adapter.UpdateQueueClock(202, sample));
        assert(adapter.AsyncStatus().state == AsyncQualificationState::NeedSerializedBaseline);
        adapter.ObserveAsyncTrial(sampleId++, SchedulerMode::AsyncCompute, 1.0, 0.0);
        assert(adapter.AsyncStatus().state == AsyncQualificationState::NeedSerializedBaseline);
    }


    {
        // The central Auto path composes independent axes from explicit capabilities. ResourceRef
        // presence is sufficient for the frame contract; legacy boolean flags are not required.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        FusionRuntime runtime(cfg);
        GameContext game{GraphicsApi::D3D12, false, true, false, true};
        FrameContext frame;
        frame.frameId = 1;
        frame.api = GraphicsApi::D3D12;
        frame.renderResolution = {1920, 1080};
        frame.outputResolution = {3840, 2160};
        frame.depth = {1, {1920, 1080}, ResourceFormat::D32Float};
        frame.motionVectors = {2, {1920, 1080}, ResourceFormat::Rg16Float};
        frame.motionVectorSource = MotionSource::Native;
        frame.motionVectorsReliable = true;
        RuntimeCapabilities caps;

        // Capability facts are fail-closed: an uninitialized host integration may not silently
        // enable any executor/path.
        TelemetrySample telemetry = sample(2.0, 7.0, 120.0, 120.0, 0.1);
        assert(!runtime.ResolveAuto(game, frame, telemetry, caps).supported);

        caps.nativeProvider = true;
        caps.preSr = true;
        caps.nativeMotion = true;
        caps.fp8 = true;
        caps.asyncCompute = true;
        caps.frameGeneration = true;
        caps.maxGenerationMultiplier = 4;

        // Central Auto requires the minimum real frame contract, not only a supported route.
        assert(!runtime.ResolveAuto(game, frame, telemetry, caps).supported);
        frame.color = {3, {1920, 1080}, ResourceFormat::Rgba16Float};
        frame.jitter.x = std::numeric_limits<float>::quiet_NaN();
        assert(!runtime.ResolveAuto(game, frame, telemetry, caps).supported);
        frame.jitter.x = 0.0f;
        frame.color.resolution = {1280, 720};
        assert(!runtime.ResolveAuto(game, frame, telemetry, caps).supported);
        frame.color.resolution = frame.renderResolution;
        telemetry.asyncComputeAvailable = true;
        telemetry.asyncComputeStable = true;
        telemetry.asyncOverlap = 0.4;
        telemetry.fgEnabled = true;
        const auto firstDecision = runtime.ResolveAuto(game, frame, telemetry, caps,
                                                       SchedulerMode::Auto, 3);
        assert(firstDecision.pipeline.supported);
        assert(firstDecision.pipeline.provider == FrameProvider::Native);
        assert(firstDecision.pipeline.transport == ProcessTransport::InProcess);
        assert(firstDecision.pipeline.motion == MotionSource::Native);
        assert(firstDecision.pipeline.placement == NrPlacement::PreSr);
        // A new structural workload re-qualifies conservatively from Serialized; the next decision
        // may enable Async once stability belongs to this exact shape/provider configuration.
        assert(firstDecision.scheduler == SchedulerMode::Serialized);
        const auto decision = runtime.ResolveAuto(game, frame, telemetry, caps,
                                                  SchedulerMode::Auto, 3);
        assert(decision.scheduler == SchedulerMode::AsyncCompute);
        assert(decision.presentation == PresentationMode::MultiFrameGeneration);
        assert(decision.generationMultiplier == 3);
        caps.maxGenerationMultiplier = 255;
        const auto cappedMfg = runtime.ResolveAuto(game, frame, telemetry, caps, SchedulerMode::Auto, 99);
        assert(cappedMfg.generationMultiplier == 4);
        GameContext noFgGame = game;
        noFgGame.frameGeneration = false;
        const auto noFg = runtime.ResolveAuto(noFgGame, frame, telemetry, caps, SchedulerMode::Auto, 4);
        assert(noFg.presentation == PresentationMode::None && noFg.generationMultiplier == 1);
        caps.maxGenerationMultiplier = 4;
        frame.api = GraphicsApi::Vulkan;
        assert(!runtime.ResolveAuto(game, frame, telemetry, caps).supported);
        frame.api = GraphicsApi::D3D12;

        // A structured motion resource must preserve provenance instead of being treated as
        // Native merely because a ResourceRef exists.
        frame.motionVectorSource = MotionSource::DlssContract;
        caps.dlssContractMotion = true;
        const auto contractMotion = runtime.ResolvePipeline(game, frame, caps);
        assert(contractMotion.motion == MotionSource::DlssContract);
        frame.motionVectorSource = MotionSource::Native;

        // DeferredResidual is an independent placement fallback when direct Pre-SR is unavailable.
        caps.preSr = false;
        caps.deferredResidual = true;
        const auto deferred = runtime.ResolvePipeline(game, frame, caps);
        assert(deferred.supported && deferred.placement == NrPlacement::DeferredResidual);
        caps.preSr = true;
        caps.deferredResidual = false;

        // Precision qualification is scoped to the full workload configuration. Changing the
        // scheduler resets the generation so timings from different critical paths cannot mix.
        const auto generationBefore = runtime.PrecisionTuner().ConfigurationGeneration();
        telemetry.asyncComputeAvailable = false;
        const auto serialized = runtime.ResolveAuto(game, frame, telemetry, caps, SchedulerMode::Auto, 3);
        assert(serialized.scheduler == SchedulerMode::Serialized);
        assert(runtime.PrecisionTuner().ConfigurationGeneration() != generationBefore);
        telemetry.asyncComputeAvailable = true;
        const auto generationAfterScheduler = runtime.PrecisionTuner().ConfigurationGeneration();
        frame.renderResolution = {2560, 1440};
        frame.color.resolution = frame.renderResolution;
        frame.depth.resolution = frame.renderResolution;
        frame.motionVectors.resolution = frame.renderResolution;
        runtime.ResolveAuto(game, frame, telemetry, caps, SchedulerMode::Serialized, 3);
        assert(runtime.PrecisionTuner().ConfigurationGeneration() != generationAfterScheduler);
        frame.renderResolution = {1920, 1080};
        frame.color.resolution = frame.renderResolution;
        frame.depth.resolution = frame.renderResolution;
        frame.motionVectors.resolution = frame.renderResolution;

        // A structural Auto transition must not initialize controller EMAs with a fake zero sample.
        // A short robust warmup uses the median of the first real timings before overload reacts.
        PerformanceConfig immediateCfg;
        immediateCfg.targetFps = 120.0;
        immediateCfg.scaleDownSustainSeconds = 0.0;
        immediateCfg.cooldownSeconds = 0.0;
        FusionRuntime immediateRuntime(immediateCfg);
        TelemetrySample overloaded = sample(1.0, 20.0, 120.0, 120.0, 0.0);
        overloaded.dtSeconds = 1.0 / 60.0;
        const auto activation = immediateRuntime.ResolveAuto(game, frame, overloaded, caps);
        assert(activation.workingScale == 1.0f && !activation.performance.changedScale);
        const auto firstMeasured = immediateRuntime.ResolveAuto(game, frame, overloaded, caps);
        assert(firstMeasured.workingScale == 1.0f && !firstMeasured.performance.changedScale);
        const auto secondMeasured = immediateRuntime.ResolveAuto(game, frame, overloaded, caps);
        assert(secondMeasured.workingScale == 1.0f && !secondMeasured.performance.changedScale);
        const auto warmedMeasured = immediateRuntime.ResolveAuto(game, frame, overloaded, caps);
        assert(warmedMeasured.performance.changedScale && warmedMeasured.workingScale < 1.0f);

        game.is32Bit = true;
        caps.x86Carrier = false;
        assert(!runtime.ResolvePipeline(game, frame, caps).supported);
        caps.x86Carrier = true;
        const auto x86 = runtime.ResolvePipeline(game, frame, caps);
        assert(x86.supported && x86.provider == FrameProvider::Native &&
               x86.transport == ProcessTransport::X86Carrier);
    }

    {
        // CPU residual reference preserves the capture exposure and normalizes historical delta
        // into the current pre-exposed domain before reprojection/composition.
        ResidualEngine engine;
        // Exposure is mandatory metadata. A default-constructed extract/image must not silently
        // pretend that preExposure=1.0 and authorize temporal residual reuse.
        ResidualExtractInput missingExposure;
        missingExposure.original = {1, 1, {1.0f}};
        missingExposure.neural = {1, 1, {2.0f}};
        missingExposure.frameId = 1;
        assert(engine.Extract(missingExposure).residual.empty());
        assert(ResidualImage{}.preExposure == 0.0f);

        ResidualExtractInput extract;
        extract.original = {2, 1, {1.0f, 2.0f}};
        extract.neural = {2, 1, {3.0f, 5.0f}};
        extract.depth = {1.0f, 1.0f};
        extract.preExposure = 2.0f;
        extract.frameId = 10;
        const auto history = engine.Extract(extract);
        assert(history.residual.size() == 2 && history.residual[0] == 2.0f && history.residual[1] == 3.0f);
        assert(history.preExposure == 2.0f && history.sourceFrame == 10);
        const auto normalized = engine.NormalizeExposure(history, 4.0f);
        assert(normalized.residual.size() == 2);
        assert(std::fabs(normalized.residual[0] - 4.0f) < 0.001f);
        assert(std::fabs(normalized.residual[1] - 6.0f) < 0.001f);
        const auto composed = engine.Compose({2, 1, {10.0f, 20.0f}}, normalized, 0.5f);
        assert(composed.values.size() == 2);
        assert(std::fabs(composed.values[0] - 12.0f) < 0.001f);
        assert(std::fabs(composed.values[1] - 23.0f) < 0.001f);
        ResidualImage missingComposeExposure = normalized;
        missingComposeExposure.preExposure = 0.0f;
        const auto rejectedCompose = engine.Compose({2, 1, {10.0f, 20.0f}}, missingComposeExposure, 1.0f);
        assert(rejectedCompose.values == std::vector<float>({10.0f, 20.0f}));
        const auto rejectedExposure = engine.NormalizeExposure(history, 1000.0f);
        assert(rejectedExposure.residual.empty());

        // Invalid exposure and finite arithmetic overflow fail closed instead of creating Inf/NaN
        // history that can poison later temporal frames.
        extract.preExposure = std::numeric_limits<float>::quiet_NaN();
        assert(engine.Extract(extract).residual.empty());
        extract.preExposure = 2.0f;
        extract.original.values = {-std::numeric_limits<float>::max(), 0.0f};
        extract.neural.values = {std::numeric_limits<float>::max(), 1.0f};
        const auto finiteExtract = engine.Extract(extract);
        assert(finiteExtract.residual.size() == 2 && std::isfinite(finiteExtract.residual[0]));
        ResidualImage huge = history;
        huge.preExposure = 1.0f;
        huge.residual = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        const auto finiteNormalized = engine.NormalizeExposure(huge, 2.0f);
        assert(finiteNormalized.residual.size() == 2 && std::isfinite(finiteNormalized.residual[0]));
        const ScalarImage hugeBase{2, 1, {std::numeric_limits<float>::max(), 1.0f}};
        const auto finiteCompose = engine.Compose(hugeBase, huge, 1.0f);
        assert(finiteCompose.values.size() == 2 && std::isfinite(finiteCompose.values[0]));

        // History must be older than the current frame. Same-frame/future residuals are never
        // accepted even when depth/motion/confidence otherwise look perfect.
        ResidualImage current = history;
        current.sourceFrame = 10;
        current.residual = {1.0f, 1.0f};
        ResidualImage sameFrame = current;
        const std::vector<float> zeroMotion(2, 0.0f);
        const std::vector<float> fullConfidence(2, 1.0f);
        const auto sameFrameResult = engine.Reproject(current, sameFrame, zeroMotion, zeroMotion,
                                                      fullConfidence, false);
        assert(sameFrameResult.valid);
        assert(sameFrameResult.historyAccepted == std::vector<std::uint8_t>({0, 0}));
        ResidualImage oldFrame = current;
        oldFrame.sourceFrame = 9;
        const auto oldFrameResult = engine.Reproject(current, oldFrame, zeroMotion, zeroMotion,
                                                     fullConfidence, false);
        assert(oldFrameResult.valid);
        assert(oldFrameResult.historyAccepted == std::vector<std::uint8_t>({1, 1}));
    }

    {
        AutoTuneConfig cfg;
        cfg.targetFps = 120.0;
        cfg.warmupSamples = 1;
        cfg.measureSamples = 2;
        cfg.scaleSteps = {1.0f, 0.5f};
        AutoTuneCoordinator tune(cfg);
        RuntimeCapabilities caps;
        tune.Start(caps, 0.5f, 1.0f);
        assert(tune.State() == AutoTuneState::Finished && !tune.Current());
        caps.fp8 = true;
        tune.Start(caps, 0.5f, 1.0f);
        // 1.0: warmup + two over-budget samples.
        tune.Observe(sample(3.0, 10.0, 100.0, 100.0, 0.1));
        tune.Observe(sample(3.0, 10.0, 100.0, 100.0, 0.1));
        tune.Observe(sample(3.0, 10.0, 100.0, 100.0, 0.1));
        // 0.5: warmup + two samples meeting target.
        tune.Observe(sample(1.5, 7.0, 130.0, 130.0, 0.1));
        tune.Observe(sample(1.5, 7.0, 130.0, 130.0, 0.1));
        tune.Observe(sample(1.5, 7.0, 130.0, 130.0, 0.1));
        assert(tune.State() == AutoTuneState::Finished);
        const auto best = tune.Best();
        assert(best && best->targetMet && std::fabs(best->candidate.workingScale - 0.5f) < 0.001f);

        // HighestQualityAtTarget keeps FP8 when FP8 and Hybrid both satisfy the target at the same
        // spatial scale, even if the lower-precision candidate benchmarks faster. Duplicate and
        // non-finite scale candidates are normalized away before the tune begins.
        AutoTuneConfig qualityCfg;
        qualityCfg.targetFps = 120.0;
        qualityCfg.warmupSamples = 1;
        qualityCfg.measureSamples = 2;
        qualityCfg.scaleSteps = {1.0f, 1.0f, std::numeric_limits<float>::quiet_NaN()};
        qualityCfg.objective = AutoTuneObjective::HighestQualityAtTarget;
        AutoTuneCoordinator qualityTune(qualityCfg);
        RuntimeCapabilities qualityCaps;
        qualityCaps.fp8 = true;
        qualityCaps.hybridNvfp4 = true;
        qualityTune.Start(qualityCaps, 1.0f, 1.0f);
        for (int i = 0; i < 3; ++i) qualityTune.Observe(sample(2.0, 8.0, 125.0, 125.0, 0.1)); // FP8
        for (int i = 0; i < 3; ++i) qualityTune.Observe(sample(1.5, 7.0, 140.0, 140.0, 0.1)); // Hybrid
        assert(qualityTune.State() == AutoTuneState::Finished);
        assert(qualityTune.Results().size() == 2);
        const auto qualityBest = qualityTune.Best();
        assert(qualityBest && qualityBest->targetMet &&
               qualityBest->candidate.precision == NrPrecision::Fp8);
    }

    {
        const auto path = std::filesystem::temp_directory_path() / "nrfusion-profiles-test.json";
        const auto backup = std::filesystem::path(path.string() + ".bak");
        const auto badPath = std::filesystem::temp_directory_path() / "nrfusion-profiles-bad.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(backup, ec);
        std::filesystem::remove(badPath, ec);
        ProfileStore store(path);
        RuntimeProfile profile;
        profile.fingerprint.gameSha256 = "gamehash";
        profile.fingerprint.gpuKey = "gpu";
        profile.fingerprint.driverKey = "driver";
        profile.fingerprint.runtimeKey = "runtime";
        profile.fingerprint.api = GraphicsApi::D3D12;
        profile.fingerprint.provider = FrameProvider::Native;
        profile.fingerprint.transport = ProcessTransport::InProcess;
        profile.fingerprint.placement = NrPlacement::PreSr;
        profile.fingerprint.motion = MotionSource::Native;
        profile.fingerprint.renderResolution = {1920, 1080};
        profile.fingerprint.outputResolution = {3840, 2160};
        profile.fingerprint.targetFps = 119.94005994005994;
        profile.fingerprint.objective = AutoTuneObjective::HighestQualityAtTarget;
        profile.chosen = {0.67f, SchedulerMode::AsyncCompute, NrPrecision::Fp8};
        profile.asyncQualified = true;
        profile.medianFrameMs = 7.5;
        assert(store.Upsert(profile));
        assert(store.Save());
        ProfileStore loaded(path);
        assert(loaded.Load());
        const auto found = loaded.Find(profile.fingerprint);
        assert(found && found->asyncQualified);
        assert(found->fingerprint == profile.fingerprint); // exact double round-trip
        assert(std::fabs(found->chosen.workingScale - 0.67f) < 0.001f);

        // Interrupted transactional save recovery: if only the backup survives, Load restores it.
        std::filesystem::rename(path, backup, ec);
        assert(!ec);
        ProfileStore recovered(path);
        assert(recovered.Load() && recovered.Find(profile.fingerprint));

        // Save and Load reject poisoned/corrupt profiles rather than silently defaulting fields.
        RuntimeProfile invalid = profile;
        invalid.medianFrameMs = std::numeric_limits<double>::quiet_NaN();
        ProfileStore invalidStore(badPath);
        assert(!invalidStore.Upsert(invalid));
        assert(invalidStore.Profiles().empty());

        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":1,"profiles":[]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[{"gameSha256":"g","gpuKey":"gpu","driverKey":"d","runtimeKey":"r","api":4,"provider":0,"transport":0,"placement":1,"motion":0,"renderWidth":1920,"renderHeight":1080,"outputWidth":3840,"outputHeight":2160,"targetFps":120,"objective":0,"workingScale":0.67,"scheduler":1.5,"precision":0,"asyncQualified":true,"precisionQualified":false,"medianFrameMs":7,"p95FrameMs":8,"medianNrMs":2,"meanQueuePressure":0.2}]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[{"gameSha256":"g","gpuKey":"gpu","driverKey":"d","runtimeKey":"r","api":4,"provider":0,"transport":0,"placement":1,"motion":0,"renderWidth":1920,"renderHeight":1080,"outputWidth":3840,"outputHeight":2160,"targetFps":120,"objective":0,"workingScale":0.67,"scheduler":1,"precision":0,"asyncQualified":trueX,"precisionQualified":false,"medianFrameMs":7,"p95FrameMs":8,"medianNrMs":2,"meanQueuePressure":0.2}]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[{"gameSha256":"g","gpuKey":"gpu","driverKey":"d","runtimeKey":"r","api":4,"provider":0,"transport":0,"placement":1,"motion":0,"renderWidth":1920,"renderHeight":1080,"outputWidth":3840,"outputHeight":2160,"targetFps":0120,"objective":0,"workingScale":0.67,"scheduler":1,"precision":0,"asyncQualified":true,"precisionQualified":false,"medianFrameMs":7,"p95FrameMs":8,"medianNrMs":2,"meanQueuePressure":0.2}]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"schema":3,"profiles":[]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[],"unknown":true})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"(garbage {"schema":3,"profiles":[]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[]} garbage)";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ifstream good(path, std::ios::binary);
            std::string json((std::istreambuf_iterator<char>(good)), std::istreambuf_iterator<char>());
            const auto schedulerPos = json.find("\"scheduler\":2");
            assert(schedulerPos != std::string::npos);
            json.insert(schedulerPos, "\"scheduler\":1,");
            std::ofstream bad(badPath, std::ios::binary | std::ios::trunc);
            bad << json;
        }
        assert(!ProfileStore(badPath).Load());

        // Persisted AutoTune candidates must be concrete and actually qualified. Auto/SecondaryGpu
        // are not measured by the current coordinator, Async requires its qualification bit, and
        // Hybrid NVFP4 requires precision qualification.
        RuntimeProfile unqualified = profile;
        unqualified.chosen.scheduler = SchedulerMode::Auto;
        assert(!invalidStore.Upsert(unqualified));
        unqualified = profile;
        unqualified.chosen.scheduler = SchedulerMode::SecondaryGpu;
        assert(!invalidStore.Upsert(unqualified));
        unqualified = profile;
        unqualified.asyncQualified = false;
        assert(!invalidStore.Upsert(unqualified));
        unqualified = profile;
        unqualified.chosen.scheduler = SchedulerMode::Serialized;
        unqualified.chosen.precision = NrPrecision::HybridNvfp4;
        unqualified.precisionQualified = false;
        assert(!invalidStore.Upsert(unqualified));

        // A profile measured under another target/resolution/provider must not match this workload.
        auto otherFingerprint = profile.fingerprint;
        otherFingerprint.targetFps = 60.0;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.outputResolution = {2560, 1440};
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.provider = FrameProvider::Bridge;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.transport = ProcessTransport::X86Carrier;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.placement = NrPlacement::DeferredResidual;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.motion = MotionSource::DlssContract;
        assert(!recovered.Find(otherFingerprint));

        // Load is atomic: one corrupt later object may not leave earlier objects visible.
        {
            std::ifstream good(path, std::ios::binary);
            std::string json((std::istreambuf_iterator<char>(good)), std::istreambuf_iterator<char>());
            const auto close = json.rfind("]");
            assert(close != std::string::npos);
            json.insert(close, ",{\"gameSha256\":\"broken\"}");
            std::ofstream bad(path, std::ios::binary | std::ios::trunc);
            bad << json;
        }
        ProfileStore atomic(path);
        assert(!atomic.Load());
        assert(atomic.Profiles().empty());

        std::filesystem::remove(path, ec);
        std::filesystem::remove(backup, ec);
        std::filesystem::remove(badPath, ec);
    }

    {
        // Versioned known-game compatibility knowledge is separate from detection and learned
        // profiles. Exact hash entries override generic executable entries, and constraints may
        // only remove capabilities that the host actually reported.
        const auto path = std::filesystem::temp_directory_path() / "nrfusion-compat-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        const std::string exactHash(64, 'a');
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":1,\"games\":["
                   "{\"exe\":\"Example.exe\",\"provider\":\"native\",\"async\":false,"
                   "\"preSr\":true,\"knownIssues\":[\"generic\"]},"
                   "{\"exe\":\"example.EXE\",\"sha256\":\"" << exactHash <<
                   "\",\"provider\":\"bridge\",\"nvof\":false,\"proxy\":\"version.dll\"}]}";
        }
        CompatibilityDatabase database(path);
        assert(database.Load());
        assert(database.Entries().size() == 2);
        const auto generic = database.Find("/games/EXAMPLE.EXE");
        assert(generic && generic->preferredProvider == FrameProvider::Native);
        assert(database.Find(R"(C:\Games\EXAMPLE.EXE)"));
        assert(generic->asyncCompute && !*generic->asyncCompute);
        const auto exact = database.Find("example.exe", exactHash);
        assert(exact && exact->preferredProvider == FrameProvider::Bridge);
        assert(exact->proxy && *exact->proxy == "version.dll");
        assert(exact->nvof && !*exact->nvof);

        RuntimeCapabilities caps;
        caps.nativeProvider = true;
        caps.bridgeProvider = true;
        caps.preSr = false;       // database true must not manufacture it
        caps.asyncCompute = true; // database false may remove it
        caps.nvof = true;
        const auto constrainedGeneric = CompatibilityDatabase::ConstrainCapabilities(caps, *generic);
        assert(!constrainedGeneric.preSr);
        assert(!constrainedGeneric.asyncCompute);
        const auto constrainedExact = CompatibilityDatabase::ConstrainCapabilities(caps, *exact);
        assert(!constrainedExact.nvof);

        GameContext game;
        game.api = GraphicsApi::Vulkan;
        game.nativeDlss = true;
        FrameContext frame;
        frame.frameId = 1;
        frame.api = GraphicsApi::Vulkan;
        frame.color = {1, {1280, 720}, ResourceFormat::Rgba16Float};
        frame.renderResolution = {1280, 720};
        frame.outputResolution = {1920, 1080};
        caps.preSr = true;
        caps.fp8 = true;
        FusionRuntime runtime;
        const auto bridge = runtime.ResolvePipeline(game, frame, caps, &*exact);
        assert(bridge.supported && bridge.provider == FrameProvider::Bridge);
        RuntimeCapabilities noBridge = caps;
        noBridge.bridgeProvider = false;
        const auto unavailable = runtime.ResolvePipeline(game, frame, noBridge, &*exact);
        assert(!unavailable.supported && unavailable.provider == FrameProvider::Unsupported);

        // Duplicate keys/entries and unknown schema fields fail closed instead of being merged.
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":1,\"games\":[{\"exe\":\"a.exe\",\"exe\":\"b.exe\"}]}";
        }
        assert(!CompatibilityDatabase(path).Load());
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":1,\"games\":[{\"exe\":\"a.exe\"},{\"exe\":\"A.EXE\"}]}";
        }
        assert(!CompatibilityDatabase(path).Load());
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":01,\"games\":[]}";
        }
        assert(!CompatibilityDatabase(path).Load());
        std::filesystem::remove(path, ec);
    }

    {
        DiagnosticsSnapshot d;
        d.frameId = 42;
        d.game.api = GraphicsApi::D3D12;
        d.decision.pipeline.provider = FrameProvider::Native;
        d.decision.pipeline.transport = ProcessTransport::InProcess;
        d.decision.pipeline.placement = NrPlacement::PreSr;
        d.decision.pipeline.motion = MotionSource::Native;
        d.decision.scheduler = SchedulerMode::AsyncCompute;
        d.decision.workingScale = std::numeric_limits<float>::quiet_NaN();
        d.telemetry.nrGpuMs = std::numeric_limits<double>::quiet_NaN();
        const auto json = Diagnostics::ToJson(d);
        const auto text = Diagnostics::ToText(d);
        assert(json.find("\"provider\":\"native\"") != std::string::npos);
        assert(json.find("\"capFp8\":false") != std::string::npos);
        assert(json.find("nan") == std::string::npos);
        assert(text.find("provider=native") != std::string::npos);
        DecisionTraceBuffer trace(2);
        trace.Push(d); trace.Push(d); trace.Push(d);
        assert(trace.Entries().size() == 2);
    }

    std::cout << "NRFusion core tests passed\n";
    // Public/current throughput getters must age too; the host uses them directly. A stalled
    // completion stream cannot keep reporting the last healthy rate forever.
    {
        TelemetryTracker tracker({0.50, 0.75, 3});
        tracker.OnSourceWork(10.00);
        tracker.OnSourceWork(10.01);
        tracker.OnNrSubmitted();
        tracker.OnNrSubmitted();
        tracker.OnNrCompleted(10.00);
        tracker.OnNrCompleted(10.01);
        const double beforeDuplicate = tracker.CurrentProcessedFps(10.02);
        tracker.OnNrCompleted(10.015); // unmatched duplicate/stale completion is ignored
        assert(tracker.CurrentProcessedFps(10.02) == beforeDuplicate);
        assert(beforeDuplicate > 50.0);
        assert(tracker.CurrentProcessedFps(12.50) < 5.0);
        assert(tracker.CurrentSourceFps(12.50) < 5.0);
    }

    // DLSS 5 Neural Rendering visual tuning: pure resolver, independent of PerformanceController/
    // WorkingScale/scheduler/precision/motion/provider/transport.
    {
        Dlss5NrCapabilities full;
        full.style = true;
        full.intensity = true;
        full.localStructure = true;
        full.skinStructure = true;
        full.automaticMask = true;

        // Default preserves the runtime's own default: no forced style, defaults for the rest.
        {
            Dlss5NeuralRenderingSettings req;
            const auto r = ResolveDlss5NeuralRendering(req, full);
            assert(r.applied.style == Dlss5Style::Default);
            assert(r.applied.intensity == 1.0f);
            assert(r.applied.localStructure == 1.0f);
            assert(!r.applied.skinStructure.has_value());
            assert(r.applied.automaticMask == TriState::Auto);
        }

        // Natural / Cinematic pass through unchanged when supported.
        {
            Dlss5NeuralRenderingSettings req;
            req.style = Dlss5Style::Natural;
            assert(ResolveDlss5NeuralRendering(req, full).applied.style == Dlss5Style::Natural);
            req.style = Dlss5Style::Cinematic;
            assert(ResolveDlss5NeuralRendering(req, full).applied.style == Dlss5Style::Cinematic);
        }

        // Intensity: min/max/default and NaN/Inf fail closed to Default (1.0), never extrapolated
        // beyond the runtime's declared 0..2 range.
        {
            Dlss5NeuralRenderingSettings req;
            req.intensity = -5.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.intensity == 0.0f);
            req.intensity = 5.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.intensity == 2.0f);
            req.intensity = 1.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.intensity == 1.0f);
            req.intensity = std::numeric_limits<float>::quiet_NaN();
            assert(ResolveDlss5NeuralRendering(req, full).applied.intensity == 1.0f);
            req.intensity = std::numeric_limits<float>::infinity();
            assert(ResolveDlss5NeuralRendering(req, full).applied.intensity == 1.0f);
            req.intensity = -std::numeric_limits<float>::infinity();
            assert(ResolveDlss5NeuralRendering(req, full).applied.intensity == 1.0f);
        }

        // Local Structure: same 0..2 / default 1.0 contract as Intensity.
        {
            Dlss5NeuralRenderingSettings req;
            req.localStructure = -1.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.localStructure == 0.0f);
            req.localStructure = 9.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.localStructure == 2.0f);
            req.localStructure = std::numeric_limits<float>::quiet_NaN();
            assert(ResolveDlss5NeuralRendering(req, full).applied.localStructure == 1.0f);
        }

        // Skin Structure: Auto (nullopt) stays Auto; a manual value is clamped into -1..2; a
        // manual NaN falls back to Auto rather than an invented number.
        {
            Dlss5NeuralRenderingSettings req;
            assert(!ResolveDlss5NeuralRendering(req, full).applied.skinStructure.has_value());
            req.skinStructure = 1.5f;
            auto r = ResolveDlss5NeuralRendering(req, full);
            assert(r.applied.skinStructure.has_value() && r.applied.skinStructure.value() == 1.5f);
            req.skinStructure = 99.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.skinStructure.value() == 2.0f);
            req.skinStructure = -99.0f;
            assert(ResolveDlss5NeuralRendering(req, full).applied.skinStructure.value() == -1.0f);
            req.skinStructure = std::numeric_limits<float>::quiet_NaN();
            assert(!ResolveDlss5NeuralRendering(req, full).applied.skinStructure.has_value());
        }

        // Automatic Mask: Auto/Off/On pass straight through when supported.
        {
            Dlss5NeuralRenderingSettings req;
            req.automaticMask = TriState::Auto;
            assert(ResolveDlss5NeuralRendering(req, full).applied.automaticMask == TriState::Auto);
            req.automaticMask = TriState::Off;
            assert(ResolveDlss5NeuralRendering(req, full).applied.automaticMask == TriState::Off);
            req.automaticMask = TriState::On;
            assert(ResolveDlss5NeuralRendering(req, full).applied.automaticMask == TriState::On);
        }

        // A capability that is absent/undetected must fail closed to Default/Auto no matter what
        // was requested -- never silently mapped to another parameter, never assumed supported.
        {
            Dlss5NrCapabilities none {}; // every field false: "runtime not detected" / old runtime.
            Dlss5NeuralRenderingSettings req;
            req.style = Dlss5Style::Cinematic;
            req.intensity = 1.8f;
            req.localStructure = 1.8f;
            req.skinStructure = 1.0f;
            req.automaticMask = TriState::On;

            const auto r = ResolveDlss5NeuralRendering(req, none);
            assert(r.requested.style == Dlss5Style::Cinematic); // requested is preserved verbatim...
            assert(r.applied.style == Dlss5Style::Default);     // ...but nothing unsupported is applied.
            assert(r.applied.intensity == 1.0f);
            assert(r.applied.localStructure == 1.0f);
            assert(!r.applied.skinStructure.has_value());
            assert(r.applied.automaticMask == TriState::Auto);
            assert(!r.styleSupported && !r.intensitySupported && !r.localStructureSupported &&
                   !r.skinStructureSupported && !r.automaticMaskSupported);
        }

        // Mixed capability: only some parameters detected as supported (e.g. a partially-probed
        // runtime). Each field fails closed independently -- support for one never implies another.
        {
            Dlss5NrCapabilities mixed;
            mixed.style = true;
            mixed.intensity = false;
            mixed.localStructure = true;
            mixed.skinStructure = false;
            mixed.automaticMask = true;

            Dlss5NeuralRenderingSettings req;
            req.style = Dlss5Style::Natural;
            req.intensity = 1.8f;
            req.localStructure = 1.8f;
            req.skinStructure = 1.0f;
            req.automaticMask = TriState::On;

            const auto r = ResolveDlss5NeuralRendering(req, mixed);
            assert(r.applied.style == Dlss5Style::Natural);      // supported: requested != default
            assert(r.applied.intensity == 1.0f);                 // unsupported: fell back to default
            assert(r.applied.localStructure == 1.8f);            // supported: passed through
            assert(!r.applied.skinStructure.has_value());        // unsupported: fell back to Auto
            assert(r.applied.automaticMask == TriState::On);     // supported: passed through
            assert(r.requested.intensity != r.applied.intensity); // requested vs. applied differ
        }
    }

    // Adaptive Exposure: source-selection hysteresis, Last Stable, Manual fallback, camera cut.
    // Portable/CPU-only: the host's real Game Exposure (`DlssNr::GameExposureStatus()`) and
    // Buffer Scan (`DlssNr::ExposureScan`, including its own anchor mapping/trim) are not
    // reimplemented here -- each ExposureMeasurement below stands in for a source's ALREADY
    // fully-resolved white point (see AdaptiveExposure.hpp file header).
    {
        // Confidence: any invalid/NaN/out-of-range component fails closed to 0 and caps the
        // combined value (a chain, not an average).
        {
            ExposureConfidence c;
            c.resourceConfidence = 1.0f;
            c.temporalConfidence = 1.0f;
            c.sceneCorrelation = 1.0f;
            c.mappingConfidence = 1.0f;
            assert(c.Combined() == 1.0f);
            c.sceneCorrelation = 0.2f;
            assert(std::abs(c.Combined() - 0.2f) < 1e-6f);
            c.mappingConfidence = std::numeric_limits<float>::quiet_NaN();
            assert(c.Combined() == 0.0f);
            ExposureConfidence bad;
            bad.resourceConfidence = -5.0f;
            bad.temporalConfidence = 5.0f; // out of [0,1]
            bad.sceneCorrelation = std::numeric_limits<float>::infinity();
            bad.mappingConfidence = 0.9f;
            assert(bad.Combined() == 0.0f); // resourceConfidence clamps to 0 and caps it.
        }

        // Game Exposure valid + confident: Auto promotes it after sustained frames, not sooner
        // (hysteresis is frame-count based, never a single-frame flip).
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames - 1; ++i) {
                auto game = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
                d = ctrl.Update(game, std::nullopt, false, 1.0 / 60.0);
                assert(d.source != ExposureSource::GameExposure);
            }
            auto game = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            d = ctrl.Update(game, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure);
            assert(d.stable && d.sameFrame);
            assert(d.whitePoint == 180.0f); // passed through unchanged: the host already resolved it.
        }

        // Invalid Game Exposure (valid=false) never counts as confident, regardless of the
        // numbers it carries.
        {
            AdaptiveExposureController ctrl;
            auto invalidGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.99f, false, true);
            ExposureDecision d;
            for (int i = 0; i < 30; ++i)
                d = ctrl.Update(invalidGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source != ExposureSource::GameExposure);
        }

        // NaN/Inf/zero white point fails closed: never treated as a confident reading.
        {
            AdaptiveExposureController ctrl;
            for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.0f, -1.0f}) {
                auto m = exposureMeasurement(ExposureSource::GameExposure, bad, 0.9f, true, true);
                ExposureDecision d;
                for (int i = 0; i < 20; ++i)
                    d = ctrl.Update(m, std::nullopt, false, 1.0 / 60.0);
                assert(d.source != ExposureSource::GameExposure);
            }
        }

        // Promote/drop hysteresis: once active, a source survives a dip shorter than
        // dropSustainFrames, and is dropped exactly once the dip reaches dropSustainFrames.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            auto weakGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.1f, true, true); // below minSourceConfidence
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure);
            for (int i = 0; i < cfg.dropSustainFrames - 1; ++i) {
                d = ctrl.Update(weakGame, std::nullopt, false, 1.0 / 60.0);
                assert(d.source == ExposureSource::GameExposure); // still sustaining through the dip
            }
            d = ctrl.Update(weakGame, std::nullopt, false, 1.0 / 60.0);
            // Dropped exactly at dropSustainFrames: no longer a fresh, stable reading -- it now
            // falls through to Last Stable, which reports the source that produced the reused
            // value (diagnostics stay honest about provenance) with stable=false and a reason.
            assert(!d.stable);
            assert(d.fallbackReason.find("Last Stable") != std::string::npos);
        }

        // Flapping input (confident/weak every other frame) never accumulates enough streak to
        // promote at all -- this is the concrete "never Game -> Buffer -> Game -> Buffer every
        // frame" guarantee.
        {
            AdaptiveExposureController ctrl;
            for (int i = 0; i < 60; ++i) {
                const bool strong = (i % 2) == 0;
                auto game = exposureMeasurement(ExposureSource::GameExposure, 180.0f, strong ? 0.9f : 0.1f, true, true);
                auto d = ctrl.Update(game, std::nullopt, false, 1.0 / 60.0);
                assert(d.source != ExposureSource::GameExposure);
            }
        }

        // Last Stable: when the active source drops, a recent decision is reused (not stable,
        // clearly flagged) until the window elapses, after which Manual fallback takes over.
        {
            AdaptiveExposureConfig cfg;
            cfg.lastStableWindowSeconds = 0.5;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ExposureDecision stableDecision;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                stableDecision = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(stableDecision.source == ExposureSource::GameExposure);

            auto nothing = std::optional<ExposureMeasurement> {};
            ExposureDecision d;
            for (int i = 0; i < cfg.dropSustainFrames; ++i)
                d = ctrl.Update(nothing, nothing, false, 1.0 / 60.0); // drops game
            // Reused Last Stable reports the source that produced the value (diagnostics stay
            // honest about provenance), flagged not-fresh via stable=false + fallbackReason --
            // never silently relabeled as "Manual" when the manual white point was never read.
            assert(d.source == ExposureSource::GameExposure);
            assert(d.whitePoint == stableDecision.whitePoint); // reused Last Stable
            assert(!d.stable);
            assert(d.fallbackReason.find("Last Stable") != std::string::npos);

            // Exhaust the window (0.5s at 60Hz ~= 30 frames) with nothing available.
            for (int i = 0; i < 40; ++i)
                d = ctrl.Update(nothing, nothing, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::Manual); // fell through to genuine Manual fallback
            assert(d.stable); // Manual is always a trustworthy, intentional value
            assert(d.fallbackReason.find("manual white point") != std::string::npos);
        }

        // Manual mode: forced, bypasses the automatic chain entirely and never consults Last
        // Stable, even if one exists. The controller does not own the manual value itself (the
        // host substitutes Config::DlssNrWhitePointScale) -- source/stable are what it reports.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0); // would-be Last Stable
            cfg.mode = ExposureSource::Manual;
            ctrl.SetConfig(cfg);
            const auto d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::Manual);
            assert(d.stable);
        }

        // Profile reload: SetConfig alone (e.g. re-applying a saved mode/adaptation) must not
        // wipe hysteresis/Last Stable state; constructing a *fresh* controller is what starts
        // clean, matching "on load: profile setting + capability -> validated applied setting"
        // without discarding in-session stability every time the config is re-applied.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure);
            AdaptiveExposureConfig reloaded = ctrl.Config();
            reloaded.dropSustainFrames = 3; // e.g. a persisted setting reloaded from a game profile
            ctrl.SetConfig(reloaded);
            d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure); // stayed active across the reload

            AdaptiveExposureController freshCtrl(reloaded);
            d = freshCtrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source != ExposureSource::GameExposure); // fresh controller requires re-sustain
        }

        // Camera cut: an authoritative same-frame Game Exposure reading snaps in immediately,
        // bypassing promotion hysteresis.
        {
            AdaptiveExposureController ctrl;
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            const auto d = ctrl.Update(strongGame, std::nullopt, /*cameraCut=*/true, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure); // on frame 1, thanks to the cut
        }

        // Regression: hysteresis sustaining the active source through a dip must not dereference
        // an absent (nullopt) measurement just because the streak says "stay". A dip with no
        // measurement at all (as opposed to a present-but-weak one) has no data to report and
        // must fall through to Last Stable instead.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            // One frame into the sustain-through-dip window, with genuinely nothing available.
            const auto d = ctrl.Update(std::nullopt, std::nullopt, false, 1.0 / 60.0);
            assert(d.whitePoint == 180.0f); // reused Last Stable, not garbage from *nullopt
            assert(!d.stable);
        }

        // Camera cut also resets Buffer Scan's hysteresis streak: a Buffer Scan source that was
        // about to be promoted must re-sustain after a cut rather than carrying its pre-cut
        // streak across the scene change (its scan candidate's temporal correlation is invalid).
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongBuffer = exposureMeasurement(ExposureSource::BufferScan, 180.0f, 0.9f, true, false);
            for (int i = 0; i < cfg.promoteSustainFrames - 1; ++i)
                ctrl.Update(std::nullopt, strongBuffer, false, 1.0 / 60.0);
            auto d = ctrl.Update(std::nullopt, strongBuffer, /*cameraCut=*/true, 1.0 / 60.0);
            assert(d.source != ExposureSource::BufferScan); // streak reset by the cut, not yet resustained
        }

        // Resource missing: nullopt measurements for both sources, with no history, fail closed
        // straight to genuine Manual (no Last Stable to fall back on yet).
        {
            AdaptiveExposureController ctrl;
            const auto d = ctrl.Update(std::nullopt, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::Manual);
            assert(d.stable);
            assert(d.fallbackReason.find("no confident automatic source") != std::string::npos);
        }

        // Forced Game Exposure mode that is unavailable must show a clear fallback, never a
        // fabricated "active" state.
        {
            AdaptiveExposureConfig cfg;
            cfg.mode = ExposureSource::GameExposure;
            AdaptiveExposureController ctrl(cfg);
            const auto d = ctrl.Update(std::nullopt, std::nullopt, false, 1.0 / 60.0);
            assert(d.source != ExposureSource::GameExposure);
            assert(d.fallbackReason.find("Game Exposure forced but unavailable") != std::string::npos);
        }

        // preExposure integration: the controller's decision is exactly the kind of value that
        // should be handed to ResidualEngine::NormalizeExposure as currentPreExposure when it
        // changes -- confirms the two systems compose without ResidualEngine's existing
        // fail-closed checks being bypassed.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.whitePoint > 0.0f && std::isfinite(d.whitePoint));

            ResidualEngine engine;
            ResidualImage history;
            history.residual.assign(4, 0.0f);
            history.width = 2;
            history.height = 2;
            history.preExposure = d.whitePoint * 1.5f; // within the engine's exposure-ratio guard
            const auto normalized = engine.NormalizeExposure(history, d.whitePoint);
            assert(normalized.preExposure == d.whitePoint);
        }
    }

    {
        // Precision is offered only where the GPU has it. Ada exposes FP8 alone, so there is no
        // cheaper rung to trade; Blackwell exposes both.
        assert(SupportedPrecisions(true, false).size() == 1);
        assert(SupportedPrecisions(true, false)[0] == NrPrecision::Fp8);
        assert(SupportedPrecisions(true, true).size() == 2);
        assert(!CheaperPrecision(NrPrecision::Fp8, true, false).has_value());
        assert(CheaperPrecision(NrPrecision::Fp8, true, true) == NrPrecision::HybridNvfp4);
        assert(!CheaperPrecision(NrPrecision::HybridNvfp4, true, true).has_value());
        assert(SupportedPrecisions(false, false).empty());
    }

    {
        // With a cheaper precision available, the first sustained over-budget episode spends the
        // precision and leaves WorkingScale where it was.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        PerformanceController controller(cfg);
        controller.SetPrecisionReliefAvailable(true);
        const float startScale = controller.WorkingScale();

        bool asked = false;
        bool movedScaleFirst = false;
        for (int i = 0; i < 400 && !asked; ++i) {
            const auto d = controller.Update(sample(9.0, 20.0, 60.0, 60.0, 0.1));
            controller.ObserveScaleCost(d.workingScale, 9.0);
            if (d.changedScale) movedScaleFirst = true;
            if (d.wantsPrecisionRelief) asked = true;
        }
        assert(asked);
        assert(!movedScaleFirst);
        assert(controller.WorkingScale() == startScale);

        // Still over budget after the trade: the next episode moves the resolution, because the
        // trade is spent and a second one would cost fidelity twice for the same episode.
        bool movedScaleAfter = false;
        for (int i = 0; i < 400 && !movedScaleAfter; ++i) {
            const auto d = controller.Update(sample(9.0, 20.0, 60.0, 60.0, 0.1));
            controller.ObserveScaleCost(d.workingScale, 9.0);
            if (d.changedScale) movedScaleAfter = true;
        }
        assert(movedScaleAfter);
        assert(controller.WorkingScale() < startScale);
    }

    {
        // Hardware with a single precision behaves exactly as before: resolution is the only lever.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        PerformanceController controller(cfg);
        controller.SetPrecisionReliefAvailable(false);
        const float startScale = controller.WorkingScale();
        bool moved = false;
        for (int i = 0; i < 400 && !moved; ++i) {
            const auto d = controller.Update(sample(9.0, 20.0, 60.0, 60.0, 0.1));
            controller.ObserveScaleCost(d.workingScale, 9.0);
            assert(!d.wantsPrecisionRelief);
            if (d.changedScale) moved = true;
        }
        assert(moved);
        assert(controller.WorkingScale() < startScale);
    }

    return 0;
}
