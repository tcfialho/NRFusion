#include "controller_test_support.hpp"

void RunControllerTestsPart05() {
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

        // The overflow path must sort a large, reverse-ordered interval set correctly.
        std::vector<GpuInterval> manyIntervals;
        manyIntervals.reserve(32);
        for (int i = 31; i >= 0; --i) {
            const double begin = 10.0 + static_cast<double>(i) * 0.3125;
            manyIntervals.push_back({begin, begin + 0.3125});
        }
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

    }
