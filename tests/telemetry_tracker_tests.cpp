#include "nrfusion/TelemetryTracker.hpp"

#include <cassert>
#include <cmath>

using namespace nrfusion;

int main() {
    {
        TelemetryTracker tracker({0.20, 1.0, 3});
        tracker.OnSourceWork(0.00);
        tracker.OnSourceWork(0.00);
        tracker.OnSourceWork(0.00);
        assert(tracker.SourceFps() == 0.0);

        tracker.OnSourceWork(0.10);
        tracker.OnSourceWork(0.10);
        assert(std::fabs(tracker.SourceFps() - 20.0) < 1e-9);

        tracker.OnSourceWork(0.25);
        assert(std::fabs(tracker.SourceFps() - 12.0) < 1e-9);

        tracker.OnSourceWork(0.35);
        assert(std::fabs(tracker.SourceFps() - 8.0) < 1e-9);
    }

    {
        TelemetryTracker tracker({0.20, 1.0, 2});
        tracker.OnNrSubmitted();
        tracker.OnNrSubmitted();
        tracker.OnNrSubmitted();
        assert(tracker.QueuePressure() == 1.0);

        tracker.OnNrCompleted(1.00);
        tracker.OnNrCompleted(1.01);
        assert(tracker.ProcessedFps() > 99.0);
        tracker.OnNrAbandoned();
        assert(tracker.Outstanding() == 0);
    }

    {
        TelemetryTracker tracker({0.20, 0.20, 3});
        tracker.OnSourceWork(1.0);
        tracker.OnSourceWork(1.1);
        const double fresh = tracker.CurrentSourceFps(1.1);
        const double aged = tracker.CurrentSourceFps(1.5);
        assert(fresh > 0.0);
        assert(aged > 0.0 && aged < fresh);
    }

    return 0;
}
