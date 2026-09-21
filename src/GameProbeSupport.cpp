#include "nrfusion/GameProbe.hpp"

namespace nrfusion {

const char* GameProbe::ApiName(GraphicsApi api) noexcept {
    switch (api) {
    case GraphicsApi::D3D9:
    case GraphicsApi::D3D10:
    case GraphicsApi::D3D11:
    case GraphicsApi::D3D12: return "d3d";
    case GraphicsApi::Vulkan: return "vulkan";
    case GraphicsApi::OpenGL: return "opengl";
    default: return "unknown";
    }
}

RuntimeCapabilities GameProbe::IntegratedCapabilities() noexcept {
    // Installer support must reflect shipped hooks; aspirational flags would produce false supported results.
    RuntimeCapabilities integrated;
    integrated.nativeProvider = true;
    integrated.bridgeProvider = true;
    integrated.syntheticD3D12 = true;
    integrated.syntheticD3D11Bridge = true;
    integrated.syntheticVulkan = true;
    integrated.x86Carrier = true;
    integrated.openGlCarrier = false;
    return integrated;
}

namespace {

// Keep install-time support independent from frame-time ProviderPolicy.
bool SyntheticUsable(GraphicsApi api, const RuntimeCapabilities& capabilities) noexcept {
    switch (api) {
    case GraphicsApi::D3D12: return capabilities.syntheticD3D12;
    case GraphicsApi::D3D11: return capabilities.syntheticD3D11Bridge;
    case GraphicsApi::Vulkan: return capabilities.syntheticVulkan;
    default: return false;
    }
}

} // namespace

GameInstallSupport GameProbe::InstallSupport(const GameProbeResult& result) noexcept {
    RuntimeCapabilities integrated;
    integrated.nativeProvider = true;
    integrated.bridgeProvider = false;
    integrated.syntheticD3D12 = false;
    integrated.syntheticD3D11Bridge = false;
    integrated.syntheticVulkan = false;
    integrated.x86Carrier = false;
    integrated.openGlCarrier = false;
    return InstallSupport(result, integrated);
}

GameInstallSupport GameProbe::InstallSupport(const GameProbeResult& result,
                                             const RuntimeCapabilities& capabilities) noexcept {
    if (result.bitness == 32 && !capabilities.x86Carrier)
        return GameInstallSupport::Unsupported32Bit;
    if (result.api == GraphicsApi::OpenGL) {
        return capabilities.openGlCarrier
            ? GameInstallSupport::Supported
            : GameInstallSupport::UnsupportedOpenGL;
    }
    if (result.api == GraphicsApi::D3D9 || result.api == GraphicsApi::D3D10)
        return GameInstallSupport::UnsupportedLegacyDirect3D;
    if (result.bitness != 32 && result.bitness != 64) return GameInstallSupport::Unknown;
    if (result.api != GraphicsApi::D3D11 && result.api != GraphicsApi::D3D12 &&
        result.api != GraphicsApi::Vulkan)
        return GameInstallSupport::Unknown;

    if (result.bitness == 32) {
        // The x86 carrier supports D3D11 synthetic capture, not native NGX or D3D12/Vulkan x86.
        return result.api == GraphicsApi::D3D11 ? GameInstallSupport::Supported
                                                 : GameInstallSupport::Unsupported32BitApi;
    }

    const GameContext game = result.ToGameContext();
    if (game.nativeDlss) {
        if ((game.api == GraphicsApi::D3D12 || game.api == GraphicsApi::Vulkan) &&
            capabilities.nativeProvider)
            return GameInstallSupport::Supported;
        if ((game.api == GraphicsApi::D3D11 || game.api == GraphicsApi::Vulkan) &&
            capabilities.bridgeProvider)
            return GameInstallSupport::Supported;
        // Synthetic remains the fallback when native or bridge acquisition is unavailable.
        if (SyntheticUsable(game.api, capabilities)) return GameInstallSupport::Supported;
        return GameInstallSupport::ProviderUnavailable;
    }

    return SyntheticUsable(game.api, capabilities) ? GameInstallSupport::Supported
                                                    : GameInstallSupport::ProviderUnavailable;
}

const char* GameProbe::InstallSupportName(GameInstallSupport support) noexcept {
    switch (support) {
    case GameInstallSupport::Supported: return "supported";
    case GameInstallSupport::Unsupported32Bit: return "32-bit";
    case GameInstallSupport::Unsupported32BitApi: return "32-bit-api";
    case GameInstallSupport::UnsupportedOpenGL: return "opengl";
    case GameInstallSupport::UnsupportedLegacyDirect3D: return "d3d9-d3d10";
    case GameInstallSupport::ProviderUnavailable: return "provider-unavailable";
    default: return "unknown";
    }
}

} // namespace nrfusion
