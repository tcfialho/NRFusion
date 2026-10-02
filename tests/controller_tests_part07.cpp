#include "controller_test_support.hpp"

void RunControllerTestsPart07() {
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


    }
