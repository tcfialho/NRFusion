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
    // This is what the *shipped* OptiScaler.dll actually hooks, not an aspirational target. The
    // real installer (tools/game_probe_main.cpp --support-exit-code) gates on this value, so a
    // flag here that is not backed by a real Present/SwapBuffers hook makes the installer promise
    // an effect the runtime cannot deliver -- silently, since nothing crashes when the toggle in
    // the menu simply does nothing.
    //
    // nativeProvider/bridgeProvider are real: apply_to_optiscaler.py patches DlssNrFeature_Dx12/Vk
    // to call the neural pass when the game's own DLSS/FSR/XeSS calls arrive, and OptiScaler's
    // stock with_dx12 bridge already relays a D3D11 game's native DLSS calls to that same D3D12
    // model. syntheticD3D12/syntheticD3D11Bridge/syntheticVulkan are real too: apply_to_optiscaler.py
    // wires EvaluateSynthetic/EvaluateSyntheticDx11/EvaluateSyntheticVk into wrapped_swapchain.cpp's
    // actual Present hook, firing whenever the game has no native DLSS contract of its own
    // (State::Instance().currentFeature == nullptr). Only OpenGL has no hook at all -- there is no
    // wglSwapBuffers/SwapBuffers interception anywhere in apply_to_optiscaler.py, so openGlCarrier
    // stays false until that hook exists.
    //
    // x86Carrier is real too, but through a different mechanism than the flags above: it is not a
    // patch inside OptiScaler.dll (a 32-bit game never loads it). tools/build_dist.ps1 builds
    // nrfusion_capture32.dll (Win32) and NRFusionHost64.exe (x64) and stages them under
    // dist/OptiScaler/NRFusion/; installer/NRFusion.nsi installs the DLL as the game's proxy and
    // the exe into $GameDir\OptiScaler\NRFusion, the exact path CaptureProvider32::Connect's
    // own-process auto-spawn expects. CaptureD3D11.cpp hooks D3D11's swapchain (Present/
    // ResizeBuffers/D3D11CreateDeviceAndSwapChain) and OMSetRenderTargets for a real depth guide;
    // see GameProbe::InstallSupport, which only calls a 32-bit game Supported when its API is
    // D3D11 -- the carrier has no D3D12 or Vulkan client and never intercepts the game's own NGX.
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
// Mirrors ProviderPolicy's per-API Synthetic gate. Kept as a separate, small helper instead of
// depending on ProviderPolicy directly: this is an install-time yes/no, not a frame-time route
// choice, and the two must be free to diverge without coupling their translation units.
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
        // The x86 carrier (nrfusion_capture32.dll) only hooks D3D11's swapchain and depth-stencil;
        // it never intercepts the game's own NGX/DLSS calls, and there is no D3D12 or Vulkan x86
        // client. It always runs its own Synthetic Neural Rendering pass on top of whatever the
        // game already presented, so the x64-specific nativeProvider/bridgeProvider/synthetic*
        // flags below describe routes the carrier cannot take and do not apply here. A distinct
        // enum value (rather than ProviderUnavailable) keeps the installer from telling a 32-bit
        // Vulkan/D3D12 game that NRFusion "could not identify a supported 64-bit" renderer.
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
        // DLSS presence does not guarantee a provider can acquire a usable contract. If neither
        // Native nor Bridge can do so, Synthetic is the final provider fallback when implemented.
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
