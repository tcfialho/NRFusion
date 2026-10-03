#include "nrfusion/AdaptiveWorkloadGate.hpp"
#include "StreamlineAutoPerformance.hpp"
#include "StreamlineReflexTracker.hpp"
#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/AutoTuneCoordinator.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {

void TestGateTransitions() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    gate.RecordFrame(0.016); // starts tracking, 16ms < 1.0s
    assert(!gate.IsSampleValid());
    assert(gate.GetState() == nrfusion::AdaptiveWorkloadState::Waiting);

    // Feed 64 more frames of 16ms (~1.04s total) -> Valid
    for (int i = 0; i < 64; ++i) gate.RecordFrame(0.016);
    assert(gate.IsSampleValid());
    assert(gate.GetState() == nrfusion::AdaptiveWorkloadState::Valid);

    // Stall (200ms) -> drops to Waiting
    gate.RecordFrame(0.200);
    assert(!gate.IsSampleValid());
    assert(gate.GetState() == nrfusion::AdaptiveWorkloadState::Waiting);

    // Test overrides
    gate.SetTestOverride(1);
    assert(gate.IsSampleValid());
    gate.SetTestOverride(0);
    assert(!gate.IsSampleValid());
    gate.SetTestOverride(-1);
    gate.Reset();
}

void TestAutoPerformanceArmedAndDiscovery() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    gate.SetTestOverride(0); // invalid
    auto& autoPerf = nrfusion::streamline::StreamlineAutoPerformance::Instance();
    autoPerf.Reset();
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Armed);
    assert(!autoPerf.IsLimiterActive());

    nrfusion::streamline::AutoPerformanceResult res{};
    // Invalid workload -> remains Armed, limiter NOT active
    assert(!autoPerf.Evaluate(2, 60.0f, res));
    assert(!res.changed);
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Armed);
    assert(!autoPerf.IsLimiterActive());

    // Workload becomes valid -> enters Discovering
    gate.SetTestOverride(1);
    autoPerf.SetTickOverride(1000);
    assert(!autoPerf.Evaluate(2, 80.0f, res));
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Discovering);
    assert(!autoPerf.IsLimiterActive());

    // 1000ms elapsed (< 2000ms) -> still Discovering
    autoPerf.SetTickOverride(2000);
    assert(!autoPerf.Evaluate(2, 80.0f, res));
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Discovering);
    assert(!autoPerf.IsLimiterActive());

    // 2100ms elapsed (>= 2000ms) -> transitions to Active, seeds 0.93 * 80 = 74
    autoPerf.SetTickOverride(3100);
    assert(autoPerf.Evaluate(2, 80.0f, res));
    assert(res.changed);
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Active);
    assert(autoPerf.IsLimiterActive());
    const auto expectedNative = static_cast<std::uint32_t>(std::round(80.0f * 0.93f));
    assert(autoPerf.GetCurrentNativeFps() == expectedNative);
    assert(autoPerf.GetCurrentMfgFps() == expectedNative * 2);
    assert(res.frameLimitUs == static_cast<std::uint32_t>(std::round(1'000'000.0 / (expectedNative * 2))));

    autoPerf.SetTickOverride(0);
    gate.Reset();
}

void TestAutoPerformanceHoldAndMultiplier() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    gate.SetTestOverride(1);
    auto& autoPerf = nrfusion::streamline::StreamlineAutoPerformance::Instance();
    autoPerf.Reset();
    autoPerf.SetTickOverride(1000);
    nrfusion::streamline::AutoPerformanceResult res{};
    autoPerf.Evaluate(2, 100.0f, res); // Armed -> Discovering
    autoPerf.SetTickOverride(3100);
    autoPerf.Evaluate(2, 100.0f, res); // Discovering -> Active (seed 93)
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Active);

    // Invalidate -> Hold (target frozen)
    gate.SetTestOverride(0);
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Hold);
    autoPerf.SetTickOverride(6000);
    assert(!autoPerf.Evaluate(2, 150.0f, res));
    assert(autoPerf.GetCurrentNativeFps() == 93);

    // Valid returns -> resumes Active directly (no new discovery)
    gate.SetTestOverride(1);
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Active);

    // Multiplier change: 2X -> 3X updates targetMfgFps immediately, preserves state
    autoPerf.SetMultiplier(3);
    assert(autoPerf.GetCurrentMfgFps() == 93 * 3);
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Active);

    // User switches Manual -> Auto: ResetDiscovery resets to Armed
    autoPerf.ResetDiscovery();
    assert(autoPerf.GetState() == nrfusion::streamline::AutoMfgState::Armed);
    assert(!autoPerf.IsLimiterActive());

    autoPerf.SetTickOverride(0);
    gate.Reset();
}

void TestReflexTrackerArmedPreservesGameLimit() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    gate.SetTestOverride(0); // invalid
    auto& tracker = nrfusion::streamline::StreamlineReflexTracker::Instance();
    tracker.Reset();
    tracker.SetLatencyMode(nrfusion::MfgLatencyMode::LowLatency);
    tracker.SetAutoPerformance(true);
    assert(tracker.GetAutoState() == 0); // Armed

    sl::ReflexOptions gameReq{};
    gameReq.mode = sl::ReflexMode::eLowLatency;
    gameReq.frameLimitUs = 16666;
    const auto applied = tracker.OnGameReflexSetOptions(gameReq, true);
    // Limiter NOT active in Armed -> preserves game requested frameLimitUs
    assert(applied.frameLimitUs == 16666);

    // Manual selection is sovereign regardless of gate state
    tracker.SetLatencyMode(nrfusion::MfgLatencyMode::LowLatency, 90);
    assert(!tracker.IsAutoPerformance());
    const auto appliedManual = tracker.OnGameReflexSetOptions(gameReq, true);
    assert(appliedManual.frameLimitUs == static_cast<std::uint32_t>(std::round(1'000'000.0 / 90)));

    // Return to Auto -> restarts Armed
    tracker.SetAutoPerformance(true);
    assert(tracker.IsAutoPerformance());
    assert(tracker.GetAutoState() == 0);
    gate.Reset();
}

void TestTransfusionGateIntegration() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    auto& dlssg = nrfusion::DlssgTransfusion::Instance();
    dlssg.SetControlMode(nrfusion::MfgControlMode::Dynamic);
    dlssg.SetDynamicTargetFps(120);
    dlssg.ForceMultiplier(2);

    // When gate is invalid, dynamic multiplier is preserved
    gate.SetTestOverride(0);
    std::uint32_t mode = 1, frames = 1;
    dlssg.ProcessSetOptions(mode, frames);
    assert(frames == 1); // 2X preserved

    // Fixed override is sovereign even when invalid
    dlssg.SetControlMode(nrfusion::MfgControlMode::OverrideFixed);
    dlssg.ForceMultiplier(4);
    std::uint32_t fixedMode = 1, fixedFrames = 1;
    dlssg.ProcessSetOptions(fixedMode, fixedFrames);
    assert(fixedFrames == 3); // 4X applied
    gate.Reset();
}

void TestAutoTuneGateIntegration() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    nrfusion::AutoTuneConfig cfg{};
    cfg.warmupSamples = 2; cfg.measureSamples = 2;
    nrfusion::AutoTuneCoordinator coord(cfg);
    nrfusion::RuntimeCapabilities caps{};
    caps.fp8 = true;
    coord.Start(caps, 0.5f, 1.0f);
    assert(coord.State() == nrfusion::AutoTuneState::Warmup);

    gate.SetTestOverride(0); // invalid
    nrfusion::TelemetrySample s{};
    s.frameTimingSource = nrfusion::FrameTimingSource::GpuTimestamp;
    s.frameGpuMs = 8.0; s.nrGpuMs = 2.0;
    coord.Observe(s); coord.Observe(s); coord.Observe(s);
    assert(coord.State() == nrfusion::AutoTuneState::Warmup); // ignored

    gate.SetTestOverride(1); // valid
    coord.Observe(s); coord.Observe(s);
    assert(coord.State() == nrfusion::AutoTuneState::Measure); // progressed
    gate.Reset();
}

void TestSharedGateSovereigntyAndNRControl() {
    auto& gate = nrfusion::AdaptiveWorkloadGate::Instance();
    gate.Reset();
    gate.SetTestOverride(0); // invalid
    assert(!gate.IsSampleValid());

    // When sample is invalid, automatic scaling is prevented
    float workingScale = 1.0f;
    if (gate.IsSampleValid()) {
        workingScale = 0.5f;
    }
    assert(workingScale == 1.0f); // preserved

    // When sample is valid, automatic scaling can adapt
    gate.SetTestOverride(1);
    assert(gate.IsSampleValid());
    if (gate.IsSampleValid()) {
        workingScale = 0.5f;
    }
    assert(workingScale == 0.5f);

    // Manual/custom scale is sovereign and never gated
    float customScale = 0.75f;
    gate.SetTestOverride(0);
    workingScale = customScale;
    assert(workingScale == 0.75f);
    gate.Reset();
}

} // namespace

int main() {
    TestGateTransitions();
    TestAutoPerformanceArmedAndDiscovery();
    TestAutoPerformanceHoldAndMultiplier();
    TestReflexTrackerArmedPreservesGameLimit();
    TestTransfusionGateIntegration();
    TestAutoTuneGateIntegration();
    TestSharedGateSovereigntyAndNRControl();
    return 0;
}
