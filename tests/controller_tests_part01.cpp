#include "controller_test_support.hpp"

void RunControllerTestsPart01() {

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

    }
