#include "nrfusion/GameProbe.hpp"
#include "nrfusion/Sha256.hpp"
#include "nrfusion/InstallerState.hpp"
#include "nrfusion/ProviderPolicy.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace nrfusion;

static void WriteFakePe(const fs::path& path, int bits, const std::string& payload,
                        std::uint32_t pe = 0x80) {
    std::vector<unsigned char> bytes(std::max<std::size_t>(512, static_cast<std::size_t>(pe) + 64), 0);
    bytes[0] = 'M'; bytes[1] = 'Z';
    bytes[0x3c] = static_cast<unsigned char>(pe & 0xff);
    bytes[0x3d] = static_cast<unsigned char>((pe >> 8) & 0xff);
    bytes[pe] = 'P'; bytes[pe + 1] = 'E';
    const std::size_t optional = pe + 24;
    const std::uint16_t magic = bits == 64 ? 0x20b : 0x10b;
    bytes[optional] = static_cast<unsigned char>(magic & 0xff);
    bytes[optional + 1] = static_cast<unsigned char>((magic >> 8) & 0xff);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

int main() {
    const fs::path root = fs::temp_directory_path() / "nrfusion-game-probe-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    // Direct evidence in the executable wins.
    WriteFakePe(root / "native.exe", 64, "xxxxD3D12.dll....D3D12CreateDevice....dxgi.dll");
    auto r = GameProbe::Probe(root / "native.exe");
    assert(r.bitness == 64 && r.api == GraphicsApi::D3D12 && !r.apiFromSibling);
    // API detection alone is not enough: without a native DLSS contract or SyntheticProvider the
    // current build must fail closed instead of claiming install support.
    assert(GameProbe::InstallSupport(r) == GameInstallSupport::ProviderUnavailable);

    // Valid PE headers are followed through e_lfanew instead of being arbitrarily limited to the
    // first 4 KiB of the file.
    WriteFakePe(root / "large_stub.exe", 64, "D3D12.dll D3D12CreateDevice", 0x2000);
    const auto largeStub = GameProbe::Probe(root / "large_stub.exe");
    assert(largeStub.bitness == 64 && largeStub.api == GraphicsApi::D3D12);

    // Capability discovery is independent from API detection and feeds provider gating.
    {
        const fs::path capsRoot = root / "capabilities";
        fs::create_directories(capsRoot);
        WriteFakePe(capsRoot / "game.exe", 64, "D3D12.dll D3D12CreateDevice dxgi.dll");
        std::ofstream(capsRoot / "nvngx_dlss.dll", std::ios::binary) << "dlss";
        std::ofstream(capsRoot / "nvngx_dlssd.dll", std::ios::binary) << "rr";
        std::ofstream(capsRoot / "nvngx_dlssg.dll", std::ios::binary) << "fg";
        std::ofstream(capsRoot / "sl.interposer.dll", std::ios::binary) << "streamline";
        std::ofstream(capsRoot / "ReShade.ini", std::ios::binary) << "reshade";
        std::ofstream(capsRoot / "OptiScaler.ini", std::ios::binary) << "optiscaler";
        std::ofstream(capsRoot / "version.dll", std::ios::binary) << "proxy";
        auto capsResult = GameProbe::Probe(capsRoot / "game.exe");
        assert(capsResult.hasDlssSr && capsResult.hasDlssRayReconstruction);
        assert(capsResult.hasDlssFrameGeneration && capsResult.hasStreamline);
        assert(capsResult.hasReShade && capsResult.hasOptiScaler && capsResult.hasNgxRuntime);
        assert(capsResult.proxyVersionOccupied);
        assert(capsResult.HasNativeDlssContract());
        assert(GameProbe::InstallSupport(capsResult) == GameInstallSupport::Supported);

        RuntimeCapabilities noNative;
        noNative.nativeProvider = false;
        assert(GameProbe::InstallSupport(capsResult, noNative) == GameInstallSupport::ProviderUnavailable);
        noNative.syntheticD3D12 = true;
        assert(GameProbe::InstallSupport(capsResult, noNative) == GameInstallSupport::Supported);
    }

    // FG runtime presence alone does not prove an SR/RR frame contract. Capability files must
    // also be regular files; directory-name spoofing cannot turn a game into Native DLSS.
    {
        const fs::path fgRoot = root / "fg-only";
        fs::create_directories(fgRoot);
        WriteFakePe(fgRoot / "game.exe", 64, "D3D12.dll D3D12CreateDevice dxgi.dll");
        std::ofstream(fgRoot / "nvngx_dlssg.dll", std::ios::binary) << "fg";
        fs::create_directory(fgRoot / "nvngx_dlss.dll");
        std::ofstream(fgRoot / "NVNGX_CUSTOM.DLL", std::ios::binary) << "ngx";
        std::error_code linkEc;
        fs::create_symlink(fgRoot / "NVNGX_CUSTOM.DLL", fgRoot / "nvngx_dlssd.dll", linkEc);
        const auto fgOnly = GameProbe::Probe(fgRoot / "game.exe");
        assert(fgOnly.hasDlssFrameGeneration && !fgOnly.hasDlssSr);
        if (!linkEc) assert(!fgOnly.hasDlssRayReconstruction);
        assert(!fgOnly.HasNativeDlssContract() && fgOnly.hasNgxRuntime);
        assert(GameProbe::InstallSupport(fgOnly) == GameInstallSupport::ProviderUnavailable);
    }

    // D3D11 with a real DLSS contract requires Bridge; x86 is an independent transport gate.
    {
        GameProbeResult bridge;
        bridge.bitness = 64; bridge.api = GraphicsApi::D3D11; bridge.hasDlssSr = true;
        bridge.importsVersionDll = true;
        RuntimeCapabilities caps;
        caps.nativeProvider = true;
        assert(GameProbe::InstallSupport(bridge, caps) == GameInstallSupport::ProviderUnavailable);
        caps.bridgeProvider = true;
        assert(GameProbe::InstallSupport(bridge, caps) == GameInstallSupport::Supported);

        // Vulkan prefers Native when available, but Bridge remains the preserved-contract fallback
        // when a direct Vulkan NR provider is not integrated.
        bridge.api = GraphicsApi::Vulkan;
        caps.nativeProvider = false;
        assert(GameProbe::InstallSupport(bridge, caps) == GameInstallSupport::Supported);
        GameContext vkGame{GraphicsApi::Vulkan, false, true, false, false};
        ProviderPolicy providerPolicy;
        assert(providerPolicy.Choose(vkGame, caps) == FrameProvider::Bridge);
        caps.nativeProvider = true;
        assert(providerPolicy.Choose(vkGame, caps) == FrameProvider::Native);
        bridge.api = GraphicsApi::D3D11;
        bridge.bitness = 32;
        assert(GameProbe::InstallSupport(bridge, caps) == GameInstallSupport::Unsupported32Bit);
        caps.x86Carrier = true;
        assert(GameProbe::InstallSupport(bridge, caps) == GameInstallSupport::Supported);
    }

    // Corrupt PE offsets must fail closed instead of wrapping the header bounds check and indexing
    // past the fixed probe buffer.
    {
        std::vector<unsigned char> malformed(128, 0);
        malformed[0] = 'M'; malformed[1] = 'Z';
        malformed[0x3c] = 0xff; malformed[0x3d] = 0xff;
        malformed[0x3e] = 0xff; malformed[0x3f] = 0xff;
        std::ofstream out(root / "malformed.exe", std::ios::binary);
        out.write(reinterpret_cast<const char*>(malformed.data()),
                  static_cast<std::streamsize>(malformed.size()));
        out.close();
        const auto malformedResult = GameProbe::Probe(root / "malformed.exe");
        assert(malformedResult.bitness == 0);
    }

    // Launcher has no API; engine DLL beside it supplies the answer.
    WriteFakePe(root / "watch_dogs.exe", 64, "launcher only");
    WriteFakePe(root / "watch_dogs_engine.dll", 64, "....d3d11.dll....D3D11CreateDevice....dxgi.dll");
    r = GameProbe::Probe(root / "watch_dogs.exe");
    assert(r.api == GraphicsApi::D3D11 && r.apiFromSibling);
    assert(r.evidenceFile.filename() == "watch_dogs_engine.dll");

    // Installed graphics runtimes/proxies must not trick a second installer run into Vulkan.
    WriteFakePe(root / "nvngx_dlssnr.dll", 64, "vulkan-1.dll vkCreateInstance vkCreateDevice vkQueuePresentKHR");
    r = GameProbe::Probe(root / "watch_dogs.exe");
    assert(r.api == GraphicsApi::D3D11);

    // A system-detection helper naming everything is ambiguous and loses to a real engine.
    WriteFakePe(root / "systemdetection64.dll", 64,
                "d3d11.dll D3D11CreateDevice vulkan-1.dll vkCreateInstance opengl32.dll wglCreateContext");
    r = GameProbe::Probe(root / "watch_dogs.exe");
    assert(r.api == GraphicsApi::D3D11);

    // D3D10.1 must count as modern Direct3D even when an incidental D3D9 marker exists.
    WriteFakePe(root / "dmc4se.exe", 32, "d3d10_1.dll d3d10.dll d3d9.dll D3D11CreateDevice");
    WriteFakePe(root / "legacy64.exe", 64, "d3d9.dll Direct3DCreate9");
    r = GameProbe::Probe(root / "legacy64.exe");
    assert(r.api == GraphicsApi::D3D9 && r.direct3DMajor == 9);
    assert(GameProbe::InstallSupport(r) == GameInstallSupport::UnsupportedLegacyDirect3D);

    r = GameProbe::Probe(root / "dmc4se.exe");
    assert(r.bitness == 32 && r.api == GraphicsApi::D3D11);
    assert(GameProbe::InstallSupport(r) == GameInstallSupport::Unsupported32Bit);

    WriteFakePe(root / "gl_game.exe", 64, "opengl32.dll wglCreateContext wglGetProcAddress");
    r = GameProbe::Probe(root / "gl_game.exe");
    assert(r.api == GraphicsApi::OpenGL);
    assert(GameProbe::InstallSupport(r) == GameInstallSupport::UnsupportedOpenGL);

    // Vulkan direct import.
    WriteFakePe(root / "vk_game.exe", 64, "vulkan-1.dll vkCreateInstance vkCreateDevice");
    r = GameProbe::Probe(root / "vk_game.exe");
    assert(r.api == GraphicsApi::Vulkan);

    // A D3D game running through a real-looking DXVK proxy must be treated as Vulkan transport.
    // Requiring both the DXVK marker and Vulkan imports keeps ordinary dxgi/ReShade/OptiScaler
    // proxies from overriding the executable's API.
    WriteFakePe(root / "dxvk_game.exe", 64, "d3d11.dll D3D11CreateDevice dxgi.dll");
    WriteFakePe(root / "dxgi.dll", 64, "DXVK runtime vulkan-1.dll vkCreateInstance vkCreateDevice");
    r = GameProbe::Probe(root / "dxvk_game.exe");
    assert(r.api == GraphicsApi::Vulkan && r.apiFromSibling);
    assert(r.evidenceFile.filename() == "dxgi.dll");
    fs::remove(root / "dxgi.dll", ec);

    // A generic proxy mentioning DXVK without Vulkan evidence is not enough to override D3D.
    WriteFakePe(root / "dxgi.dll", 64, "DXVK compatibility note only");
    r = GameProbe::Probe(root / "dxvk_game.exe");
    assert(r.api == GraphicsApi::D3D11 && !r.apiFromSibling);
    fs::remove(root / "dxgi.dll", ec);

    // A symlink cannot become indirect DXVK/API evidence; capability probing is fail-closed on
    // linked files just like installer state handling.
    const fs::path realDxvk = root / "linked-target" / "real-dxvk.dll";
    fs::create_directories(realDxvk.parent_path());
    WriteFakePe(realDxvk, 64, "DXVK runtime vulkan-1.dll vkCreateInstance vkCreateDevice");
    std::error_code dxvkLinkEc;
    fs::create_symlink(realDxvk, root / "dxgi.dll", dxvkLinkEc);
    if (!dxvkLinkEc) {
        r = GameProbe::Probe(root / "dxvk_game.exe");
        assert(r.api == GraphicsApi::D3D11 && !r.apiFromSibling && !r.hasDxvk);
        fs::remove(root / "dxgi.dll", ec);
    }
    fs::remove(realDxvk, ec);


    // Portable SHA-256 is used by the NSIS installer for safe reinstall/uninstall without external plugins.
    {
        const std::string abc = "abc";
        const auto* first = reinterpret_cast<const std::uint8_t*>(abc.data());
        assert(Sha256Hex(std::span<const std::uint8_t>(first, abc.size())) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        std::ofstream hashFile(root / "hash.txt", std::ios::binary);
        hashFile << abc;
        hashFile.close();
        const auto fileHash = Sha256File(root / "hash.txt");
        assert(fileHash && *fileHash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        assert(Sha256FileEquals(root / "hash.txt", "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    }


#include "game_probe_installer_state_tests.inc"
#include "game_probe_delay_import_tests.inc"
    // IntegratedCapabilities exposes only the D3D11 bridge actually embedded in the standalone
    // version.dll carrier. Other providers remain harness-only until they have a shipped hook.
    {
        const auto integrated = GameProbe::IntegratedCapabilities();
        assert(!integrated.nativeProvider && integrated.bridgeProvider &&
               !integrated.syntheticD3D12 && integrated.syntheticD3D11Bridge &&
               !integrated.syntheticVulkan && !integrated.x86Carrier && !integrated.openGlCarrier);

        GameProbeResult d3d11Proxy;
        d3d11Proxy.bitness = 64;
        d3d11Proxy.api = GraphicsApi::D3D11;
        d3d11Proxy.importsVersionDll = true;
        assert(GameProbe::InstallSupport(d3d11Proxy, integrated) == GameInstallSupport::Supported);
        d3d11Proxy.importsVersionDll = false;
        assert(GameProbe::InstallSupport(d3d11Proxy, integrated) == GameInstallSupport::ProviderUnavailable);

        const auto nativeProbe = GameProbe::Probe(root / "native.exe");
        assert(GameProbe::InstallSupport(nativeProbe, integrated) == GameInstallSupport::ProviderUnavailable);

        const auto vkProbe = GameProbe::Probe(root / "vk_game.exe");
        assert(GameProbe::InstallSupport(vkProbe, integrated) == GameInstallSupport::ProviderUnavailable);

        const auto legacyProbe = GameProbe::Probe(root / "legacy64.exe");
        assert(GameProbe::InstallSupport(legacyProbe, integrated) == GameInstallSupport::UnsupportedLegacyDirect3D);

        // OpenGL has no hook surface in this OptiScaler fork at all (no wglSwapBuffers hook), and
        // SyntheticOpenGlProvider is not even copied into the patched checkout.
        const auto glProbe = GameProbe::Probe(root / "gl_game.exe");
        assert(GameProbe::InstallSupport(glProbe, integrated) == GameInstallSupport::UnsupportedOpenGL);

        // This distribution does not ship a Win32 carrier, so the bitness gate remains fail-closed.
        WriteFakePe(root / "gl_game32.exe", 32, "opengl32.dll wglCreateContext");
        const auto gl32Probe = GameProbe::Probe(root / "gl_game32.exe");
        assert(GameProbe::InstallSupport(gl32Probe, integrated) == GameInstallSupport::Unsupported32Bit);
    }

    fs::remove_all(root, ec);
    return 0;
}
