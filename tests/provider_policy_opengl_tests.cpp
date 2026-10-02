#include "nrfusion/ProviderPolicy.hpp"

#include <cassert>

using namespace nrfusion;

int main() {
    ProviderPolicy policy;
    GameContext game{};
    game.api = GraphicsApi::OpenGL;

    RuntimeCapabilities capabilities{};
    capabilities.syntheticD3D12 = true;
    capabilities.syntheticD3D11Bridge = true;
    capabilities.syntheticVulkan = true;
    assert(policy.Choose(game, capabilities) ==
           FrameProvider::Unsupported);
    assert(policy.Choose(
               game, capabilities, FrameProvider::Synthetic) ==
           FrameProvider::Unsupported);

    capabilities.openGlCarrier = true;
    assert(policy.Choose(game, capabilities) ==
           FrameProvider::Synthetic);
    assert(policy.Choose(
               game, capabilities, FrameProvider::Synthetic) ==
           FrameProvider::Synthetic);
    return 0;
}
