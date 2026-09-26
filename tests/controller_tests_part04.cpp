#include "controller_test_support.hpp"

void RunControllerTestsPart04() {
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
            if (!burst.empty()) assert(ticket.id == burst.back().id + 1);
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

    }
