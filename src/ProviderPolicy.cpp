#include "nrfusion/ProviderPolicy.hpp"

namespace nrfusion {
namespace {

bool Usable(FrameProvider provider, const GameContext& game,
            const RuntimeCapabilities& capabilities) noexcept {
    switch (provider) {
    case FrameProvider::Native:
        return game.nativeDlss &&
               (game.api == GraphicsApi::D3D12 || game.api == GraphicsApi::Vulkan) &&
               capabilities.nativeProvider;
    case FrameProvider::Bridge:
        return game.nativeDlss &&
               (game.api == GraphicsApi::D3D11 || game.api == GraphicsApi::Vulkan) &&
               capabilities.bridgeProvider;
    case FrameProvider::Synthetic:
        // Synthetic is the fallback for "no usable DLSS contract", not merely "no DLSS DLL".
        // Selection order tries Native/Bridge first, so allowing Synthetic here also covers games
        // where DLSS is present but no compatible contract provider is available on this host.
        // Each API has its own real hook and its own capability -- one working route must never
        // be read as proof that the others work too.
        switch (game.api) {
        case GraphicsApi::D3D12: return capabilities.syntheticD3D12;
        case GraphicsApi::D3D11: return capabilities.syntheticD3D11Bridge;
        case GraphicsApi::Vulkan: return capabilities.syntheticVulkan;
        default: return false;
        }
    case FrameProvider::Unsupported:
        return false;
    }
    return false;
}

} // namespace

FrameProvider ProviderPolicy::Choose(const GameContext& game,
                                     const RuntimeCapabilities& capabilities,
                                     std::optional<FrameProvider> preferred) const noexcept {
    if (preferred) return Usable(*preferred, game, capabilities) ? *preferred : FrameProvider::Unsupported;

    if (Usable(FrameProvider::Native, game, capabilities)) return FrameProvider::Native;
    if (Usable(FrameProvider::Bridge, game, capabilities)) return FrameProvider::Bridge;
    if (Usable(FrameProvider::Synthetic, game, capabilities)) return FrameProvider::Synthetic;
    return FrameProvider::Unsupported;
}

} // namespace nrfusion
