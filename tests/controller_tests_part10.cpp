#include "controller_test_support.hpp"

void RunControllerTestsPart10() {
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

    }
