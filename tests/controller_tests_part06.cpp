#include "controller_test_support.hpp"

void RunControllerTestsPart06() {
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

    }
