#include "nrfusion/D3D12NrFramePlan.hpp"

#include <cassert>
#include <cmath>
#include <limits>

using namespace nrfusion;

namespace {
D3D12NrFramePlanInput Base() {
    D3D12NrFramePlanInput in;
    in.colorSurface = {1920, 1080};
    in.depthSurface = {1920, 1080};
    in.motionSurface = {1920, 1080};
    in.activeColor = {0, 0, 1920, 1080};
    in.depth = {0, 0, 1920, 1080};
    in.motion = {0, 0, 1920, 1080};
    return in;
}
}

int main() {
    D3D12NrFramePlan plan;
    auto in = Base();
    assert(BuildD3D12NrFramePlan(in, plan));
    assert((plan.work == Resolution{1920, 1080}));
    assert(plan.requestedPasses == 1);
    assert(!plan.reduced && !plan.cropColor);

    in.execution.workingScale = 0.5f;
    in.execution.passes = 9;
    assert(BuildD3D12NrFramePlan(in, plan));
    assert((plan.work == Resolution{960, 540}));
    assert(plan.requestedPasses == 3);
    assert(plan.reduced);
    assert(std::fabs(plan.motionToWorkX - 0.5f) < 0.0001f);

    in.execution.workingScale = 1.5f;
    in.execution.unlockPasses = true;
    in.execution.passes = 31;
    assert(BuildD3D12NrFramePlan(in, plan));
    assert((plan.work == Resolution{2880, 1620}));
    assert(plan.requestedPasses == 30);

    in.execution.proxyBackend = true;
    assert(BuildD3D12NrFramePlan(in, plan));
    assert(plan.requestedPasses == 1);

    in.execution.proxyBackend = false;
    in.execution.workingScale = std::numeric_limits<float>::quiet_NaN();
    assert(BuildD3D12NrFramePlan(in, plan));
    assert(plan.workingScale == 1.0f);
    assert((plan.work == Resolution{1920, 1080}));

    in = Base();
    in.beforeUpscale = true;
    in.activeColor = {0, 0, 1280, 720};
    assert(BuildD3D12NrFramePlan(in, plan));
    assert(plan.cropColor);
    in.beforeUpscale = false;
    assert(BuildD3D12NrFramePlan(in, plan));
    assert(!plan.cropColor);

    in = Base();
    in.depth = {1900, 0, 64, 64};
    assert(!BuildD3D12NrFramePlan(in, plan));

    in = Base();
    in.motion = {17, 9, 1280, 720};
    assert(BuildD3D12NrFramePlan(in, plan));

    in.activeColor.width = 0;
    assert(!BuildD3D12NrFramePlan(in, plan));

    return 0;
}
