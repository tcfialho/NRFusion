#include "nrfusion/RuntimeMenuModel.hpp"
#include "nrfusion/RuntimeShell.hpp"

#include <cassert>
#include <limits>

using namespace nrfusion;

int main() {
    RuntimeShell runtime;
    RuntimeConfig active;
    active.generation = 2;
    assert(runtime.Initialize(active));

    const RuntimeStatus before = runtime.Status();
    const RuntimeConfig activeBefore = runtime.Config();

    RuntimeMenuModel menu;
    assert(menu.Open(activeBefore));
    assert(menu.Visible());
    assert(!menu.Dirty());
    assert(runtime.Status().state == before.state);
    assert(runtime.Status().configGeneration == before.configGeneration);
    assert(runtime.Config() == activeBefore);

    menu.Close();
    assert(!menu.Visible());
    assert(runtime.Config() == activeBefore);

    assert(menu.Open(activeBefore));
    RuntimeConfig edited = menu.Draft();
    edited.enabled = true;
    edited.mode = RuntimeNrMode::Performance;
    edited.targetFps = 120.0f;
    edited.displayHz = 165.0f;
    edited.mfgMode = RuntimeMfgMode::Fixed;
    edited.mfgQuality = RuntimeMfgQuality::Enhanced;
    edited.mfgMultiplier = 4;
    assert(menu.Stage(edited));
    assert(menu.Dirty());
    assert(runtime.Config() == activeBefore);

    RuntimeConfig invalid = edited;
    invalid.targetFps = std::numeric_limits<float>::quiet_NaN();
    assert(!menu.Stage(invalid));
    assert(menu.Draft().targetFps == 120.0f);

    RuntimeConfig committed;
    assert(!menu.ProposeCommit(2, committed));
    assert(menu.ProposeCommit(3, committed));
    assert(menu.Dirty());
    assert(committed.generation == 3);
    assert(committed.targetFps == 120.0f);
    assert(committed.displayHz == 165.0f);
    assert(committed.mfgMultiplier == 4);

    assert(runtime.Config() == activeBefore);
    assert(runtime.Reconfigure(committed));
    assert(menu.Accept(committed));
    assert(!menu.Dirty());
    assert(runtime.Status().state == RuntimeState::Running);
    assert(runtime.Status().configGeneration == 3);
    assert(runtime.Config() == committed);

    assert(menu.Stage(menu.Draft()));
    assert(!menu.Dirty());
    assert(!menu.ProposeCommit(4, committed));

    return 0;
}
