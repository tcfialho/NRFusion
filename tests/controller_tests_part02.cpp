#include "controller_test_support.hpp"

void RunControllerTestsPart02() {
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

    }
