#include "controller_test_support.hpp"

void RunControllerTestsPart12() {
{
        // Precision is offered only where the GPU has it. Ada exposes FP8 alone, so there is no
        // cheaper rung to trade; Blackwell exposes both.
        assert(SupportedPrecisions(true, false).size() == 1);
        assert(SupportedPrecisions(true, false)[0] == NrPrecision::Fp8);
        assert(SupportedPrecisions(true, true).size() == 2);
        assert(!CheaperPrecision(NrPrecision::Fp8, true, false).has_value());
        assert(CheaperPrecision(NrPrecision::Fp8, true, true) == NrPrecision::HybridNvfp4);
        assert(!CheaperPrecision(NrPrecision::HybridNvfp4, true, true).has_value());
        assert(SupportedPrecisions(false, false).empty());
    }

    {
        // With a cheaper precision available, the first sustained over-budget episode spends the
        // precision and leaves WorkingScale where it was.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        PerformanceController controller(cfg);
        controller.SetPrecisionReliefAvailable(true);
        const float startScale = controller.WorkingScale();

        bool asked = false;
        bool movedScaleFirst = false;
        for (int i = 0; i < 400 && !asked; ++i) {
            const auto d = controller.Update(sample(9.0, 20.0, 60.0, 60.0, 0.1));
            controller.ObserveScaleCost(d.workingScale, 9.0);
            if (d.changedScale) movedScaleFirst = true;
            if (d.wantsPrecisionRelief) asked = true;
        }
        assert(asked);
        assert(!movedScaleFirst);
        assert(controller.WorkingScale() == startScale);

        // Still over budget after the trade: the next episode moves the resolution, because the
        // trade is spent and a second one would cost fidelity twice for the same episode.
        bool movedScaleAfter = false;
        for (int i = 0; i < 400 && !movedScaleAfter; ++i) {
            const auto d = controller.Update(sample(9.0, 20.0, 60.0, 60.0, 0.1));
            controller.ObserveScaleCost(d.workingScale, 9.0);
            if (d.changedScale) movedScaleAfter = true;
        }
        assert(movedScaleAfter);
        assert(controller.WorkingScale() < startScale);
    }

    {
        // Hardware with a single precision behaves exactly as before: resolution is the only lever.
        PerformanceConfig cfg;
        cfg.targetFps = 120.0;
        PerformanceController controller(cfg);
        controller.SetPrecisionReliefAvailable(false);
        const float startScale = controller.WorkingScale();
        bool moved = false;
        for (int i = 0; i < 400 && !moved; ++i) {
            const auto d = controller.Update(sample(9.0, 20.0, 60.0, 60.0, 0.1));
            controller.ObserveScaleCost(d.workingScale, 9.0);
            assert(!d.wantsPrecisionRelief);
            if (d.changedScale) moved = true;
        }
        assert(moved);
        assert(controller.WorkingScale() < startScale);
    }

    }
