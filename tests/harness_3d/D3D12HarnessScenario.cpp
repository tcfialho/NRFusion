#include "D3D12TestHarness.hpp"

#include "nrfusion/RuntimeShell.hpp"

#include <algorithm>
#include <iostream>

namespace nrfusion::testing {
namespace {

bool ValidateSteady(D3D12TestHarness& harness) {
    const ProviderInput input{1, 1};
    const auto frame = harness.AcquireFrame(input);
    GameContext game{};
    game.api = GraphicsApi::D3D12;
    return harness.IsSupported(game) && frame.ReadyForCore();
}

bool ValidateReset(D3D12TestHarness& harness) {
    const auto cut = harness.AcquireFrame({45, 45});
    auto reset = harness.AcquireFrame({46, 46});
    reset.resetHistory = true;
    return cut.cameraCut && !cut.resetHistory && !reset.cameraCut && reset.resetHistory;
}

bool ValidateMissingGuides(D3D12TestHarness& harness) {
    auto frame = harness.AcquireFrame({2, 2});
    frame.depth = {};
    frame.motionVectors = {};
    frame.exposure = {};
    frame.reactiveMask = {};
    frame.depthReliable = false;
    frame.motionVectorsReliable = false;
    return frame.ReadyForCore() && frame.EffectiveMotionSource() == MotionSource::Zero &&
           !frame.DepthReliable() && !frame.ExposureReliable();
}

bool ValidateProvenance(D3D12TestHarness& harness) {
    auto frame = harness.AcquireFrame({3, 3});
    frame.color.provenance = ResourceProvenance::GameNative;
    frame.color.reliability = ResourceReliability::Reliable;
    frame.color.ownership = ResourceOwnership::Borrowed;
    frame.color.lifetime = ResourceLifetime::Frame;
    frame.color.sourceFrameId = frame.frameId;
    if (!frame.ReadyForCore()) return false;
    frame.color.sourceFrameId = frame.frameId + 1;
    return !frame.ReadyForCore();
}

bool ValidateToggle() {
    RuntimeShell shell;
    RuntimeConfig config{};
    if (!shell.Initialize(config) || shell.Status().state != RuntimeState::Disabled) return false;
    config.generation = 2;
    config.enabled = true;
    if (!shell.Reconfigure(config) || shell.Status().state != RuntimeState::Running) return false;
    config.generation = 3;
    config.enabled = false;
    return shell.Reconfigure(config) && shell.Status().state == RuntimeState::Disabled;
}

bool ValidateFailure(D3D12TestHarness& harness) {
    auto frame = harness.AcquireFrame({4, 4});
    frame.color.opaqueId = 0;
    return !frame.ReadyForCore();
}

const char* ScenarioName(HarnessScenario scenario) {
    switch (scenario) {
    case HarnessScenario::All: return "all";
    case HarnessScenario::Steady: return "steady";
    case HarnessScenario::Resize: return "resize";
    case HarnessScenario::Reset: return "reset";
    case HarnessScenario::MissingGuides: return "missing-guides";
    case HarnessScenario::Provenance: return "provenance";
    case HarnessScenario::Toggle: return "on-off";
    case HarnessScenario::Failure: return "failure";
    default: return "legacy";
    }
}

}

bool D3D12TestHarness::RunScenario() {
    const auto selected = config_.scenario;
    const auto wants = [selected](HarnessScenario scenario) {
        return selected == HarnessScenario::All || selected == scenario;
    };

    bool ok = true;
    if (wants(HarnessScenario::Steady)) ok = ValidateSteady(*this) && ok;
    if (wants(HarnessScenario::Reset)) ok = ValidateReset(*this) && ok;
    if (wants(HarnessScenario::MissingGuides)) ok = ValidateMissingGuides(*this) && ok;
    if (wants(HarnessScenario::Provenance)) ok = ValidateProvenance(*this) && ok;
    if (wants(HarnessScenario::Toggle)) ok = ValidateToggle() && ok;
    if (wants(HarnessScenario::Failure)) ok = ValidateFailure(*this) && ok;

    if (wants(HarnessScenario::Resize)) {
        const auto originalWidth = config_.width;
        const auto originalHeight = config_.height;
        const auto width = std::max<std::uint32_t>(1, originalWidth / 2);
        const auto height = std::max<std::uint32_t>(1, originalHeight / 2);

        const bool resizedOk = ResizeResources(width, height);
        const auto resized = AcquireFrame({5, 5});
        const bool resizedFrameOk = resizedOk && resized.ReadyForCore() &&
            resized.renderResolution == Resolution{width, height};

        const bool restoredResources = ResizeResources(originalWidth, originalHeight);
        const auto restored = AcquireFrame({6, 6});
        const bool restoredFrameOk = restoredResources && restored.ReadyForCore() &&
            restored.renderResolution == Resolution{originalWidth, originalHeight};

        ok = resizedFrameOk && restoredFrameOk && ok;
    }

    std::cout << "[Harness 3D] scenario=" << ScenarioName(selected)
              << " result=" << (ok ? "PASS" : "FAIL") << "\n";
    return ok;
}

} // namespace nrfusion::testing
