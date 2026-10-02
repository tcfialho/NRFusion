#include "controller_test_support.hpp"

void RunControllerTestsPart03() {
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

    }
