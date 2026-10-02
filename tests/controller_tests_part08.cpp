#include "controller_test_support.hpp"

void RunControllerTestsPart08() {
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

    }
