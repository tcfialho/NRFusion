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


    // Installer state backs up pre-existing managed files, fingerprints the post-install state,
    // restores originals on uninstall, and preserves files the user changed after installation.
    {
        const fs::path installRoot = root / "installer-state";
        const fs::path backup = installRoot / ".backup";
        const fs::path distManifest = root / "dist-manifest.txt";
        const fs::path installedManifest = root / "installed-manifest.txt";
        fs::create_directories(installRoot / "OptiScaler");
        std::ofstream(installRoot / "dxgi.dll", std::ios::binary) << "old proxy";
        std::ofstream(installRoot / "OptiScaler" / "existing.bin", std::ios::binary) << "old runtime";

        const auto hashText = [](const std::string& v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            return Sha256Hex(std::span<const std::uint8_t>(p, v.size()));
        };
        {
            std::ofstream m(distManifest);
            m << hashText("new proxy") << "  OptiScaler.dll\n";
            m << hashText("new runtime") << "  OptiScaler/existing.bin\n";
            m << hashText("created") << "  OptiScaler/new.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(installRoot, "dxgi.dll", distManifest, backup);
        assert(st && st.backedUp == 2);

        // O manifesto e o SHA256SUMS da distribuicao, que lista tudo que foi empacotado. O banco
        // de testes vai no pacote mas nao e copiado para o diretorio do jogo: contabiliza-lo aqui
        // produziria um arquivo gerenciado que nunca existe.
        {
            const fs::path testbedManifest = root / "testbed-manifest.txt";
            const fs::path testbedRoot = root / "installer-state-testbed";
            const fs::path testbedBackup = testbedRoot / ".backup";
            fs::create_directories(testbedRoot / "OptiScaler");
            std::ofstream(testbedRoot / "OptiScaler" / "existing.bin", std::ios::binary) << "old runtime";
            {
                std::ofstream m(testbedManifest);
                m << hashText("new runtime") << "  OptiScaler/existing.bin" << "\n";
                m << hashText("testbed") << "  RequiemGame/RequiemGame.exe" << "\n";
                m << hashText("asset") << "  RequiemGame/assets/frame.jpeg" << "\n";
            }
            auto testbedState = InstallerState::SnapshotExisting(testbedRoot, "dxgi.dll", testbedManifest, testbedBackup);
            assert(testbedState && testbedState.backedUp == 1);
            std::ofstream(testbedRoot / "OptiScaler" / "existing.bin", std::ios::binary | std::ios::trunc) << "new runtime";
            const fs::path testbedInstalled = root / "testbed-installed.txt";
            testbedState = InstallerState::RecordInstalled(testbedRoot, "dxgi.dll", testbedManifest, testbedInstalled, testbedBackup);
            assert(testbedState && testbedState.recorded == 1);
            assert(!fs::exists(testbedRoot / "RequiemGame"));
        }

        std::ofstream(installRoot / "dxgi.dll", std::ios::binary | std::ios::trunc) << "new proxy";
        std::ofstream(installRoot / "OptiScaler" / "existing.bin", std::ios::binary | std::ios::trunc) << "new runtime";
        std::ofstream(installRoot / "OptiScaler" / "new.bin", std::ios::binary) << "created";
        st = InstallerState::RecordInstalled(installRoot, "dxgi.dll", distManifest, installedManifest, backup);
        assert(st && st.recorded == 3);

        // User edits one installed file. Uninstall must preserve it while restoring/removing untouched files.
        std::ofstream(installRoot / "OptiScaler" / "existing.bin", std::ios::binary | std::ios::trunc) << "user edit";
        st = InstallerState::RestoreOrRemove(installRoot, "dxgi.dll", installedManifest, backup);
        assert(st && st.restored == 1 && st.removed == 1 && st.preservedModified == 1);
        std::ifstream proxyIn(installRoot / "dxgi.dll", std::ios::binary);
        std::string proxyText((std::istreambuf_iterator<char>(proxyIn)), {});
        assert(proxyText == "old proxy");
        assert(!fs::exists(installRoot / "OptiScaler" / "new.bin"));
        std::ifstream editedIn(installRoot / "OptiScaler" / "existing.bin", std::ios::binary);
        std::string editedText((std::istreambuf_iterator<char>(editedIn)), {});
        assert(editedText == "user edit");
    }


    // Baseline survives upgrades: an original file is never replaced by a previous NRFusion build
    // in the backup, originally-absent files remain marked absent, and files removed from a newer
    // package stay tracked until uninstall.
    {
        const fs::path game = root / "upgrade-state";
        const fs::path backup = game / "OptiScaler" / "NRFusion" / "backup";
        const fs::path distV1 = root / "dist-v1.txt";
        const fs::path distV2 = root / "dist-v2.txt";
        const fs::path installed = game / "OptiScaler" / "NRFusion" / "installed.sha256";
        fs::create_directories(game / "OptiScaler" / "NRFusion");
        std::ofstream(game / "dxgi.dll", std::ios::binary) << "game original proxy";

        const auto hashText = [](const std::string& v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            return Sha256Hex(std::span<const std::uint8_t>(p, v.size()));
        };
        {
            std::ofstream m(distV1);
            m << hashText("fusion proxy v1") << "  OptiScaler.dll\n";
            m << hashText("v1-only") << "  OptiScaler/v1-only.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV1, backup);
        assert(st && st.backedUp == 1);
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "fusion proxy v1";
        fs::create_directories(game / "OptiScaler");
        std::ofstream(game / "OptiScaler" / "v1-only.bin", std::ios::binary) << "v1-only";
        st = InstallerState::RecordInstalled(game, "dxgi.dll", distV1, installed, backup);
        assert(st && st.recorded == 2);

        // Upgrade: v1-only disappears from the new distribution and v2-only is introduced.
        {
            std::ofstream m(distV2);
            m << hashText("fusion proxy v2") << "  OptiScaler.dll\n";
            m << hashText("v2-only") << "  OptiScaler/v2-only.bin\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV2, backup);
        assert(st && st.backedUp == 0); // original proxy backup must not be overwritten by v1
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "fusion proxy v2";
        std::ofstream(game / "OptiScaler" / "v2-only.bin", std::ios::binary) << "v2-only";
        st = InstallerState::RecordInstalled(game, "dxgi.dll", distV2, installed, backup);
        assert(st && st.recorded == 2 && st.removed == 1); // v1-only was pruned safely during upgrade
        assert(!fs::exists(game / "OptiScaler" / "v1-only.bin"));

        st = InstallerState::RestoreOrRemove(game, "dxgi.dll", installed, backup);
        assert(st && st.restored == 1 && st.removed == 1 && st.preservedModified == 0);
        std::ifstream restored(game / "dxgi.dll", std::ios::binary);
        std::string restoredText((std::istreambuf_iterator<char>(restored)), {});
        assert(restoredText == "game original proxy");
        assert(!fs::exists(game / "OptiScaler" / "v1-only.bin"));
        assert(!fs::exists(game / "OptiScaler" / "v2-only.bin"));
        assert(!fs::exists(backup));
    }



    // Failed upgrades are transactional: restore the immediately previous NRFusion version and its
    // metadata, not the immutable game-original baseline used by a later uninstall.
    {
        const fs::path game = root / "transaction-state";
        const fs::path state = game / "OptiScaler" / "NRFusion";
        const fs::path backup = state / "backup";
        const fs::path installed = state / "installed.sha256";
        const fs::path tx = state / "transaction";
        const fs::path distV1 = root / "tx-v1.txt";
        const fs::path distV2 = root / "tx-v2.txt";
        fs::create_directories(state);
        std::ofstream(game / "dxgi.dll", std::ios::binary) << "original proxy";
        std::ofstream(game / "NRFusion.install.ini", std::ios::binary) << "marker-v1";
        std::ofstream(state / "NRFusionProbe.exe", std::ios::binary) << "probe-v1";
        std::ofstream(state / "dist.sha256", std::ios::binary) << "dist-v1";
        std::ofstream(state / "Uninstall.exe", std::ios::binary) << "uninstall-v1";

        const auto hashText = [](const std::string& v) {
            const auto* pbytes = reinterpret_cast<const std::uint8_t*>(v.data());
            return Sha256Hex(std::span<const std::uint8_t>(pbytes, v.size()));
        };
        {
            std::ofstream m(distV1);
            m << hashText("proxy-v1") << "  OptiScaler.dll\n";
            m << hashText("runtime-v1") << "  OptiScaler/runtime.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV1, backup);
        assert(st);
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "proxy-v1";
        fs::create_directories(game / "OptiScaler");
        std::ofstream(game / "OptiScaler" / "runtime.bin", std::ios::binary) << "runtime-v1";
        st = InstallerState::RecordInstalled(game, "dxgi.dll", distV1, installed, backup);
        assert(st);
        const std::string installedV1 = [&] {
            std::ifstream in(installed, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }();

        {
            std::ofstream m(distV2);
            m << hashText("proxy-v2") << "  OptiScaler.dll\n";
            m << hashText("runtime-v2") << "  OptiScaler/runtime.bin\n";
            m << hashText("new-v2") << "  OptiScaler/new-v2.bin\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV2, backup);
        assert(st); // immutable original baseline remains untouched
        st = InstallerState::SnapshotTransaction(game, "dxgi.dll", distV2, installed, tx);
        assert(st && fs::exists(tx / "magic.txt"));

        // Simulate a half-applied v2 plus damaged transaction metadata.
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "proxy-v2";
        std::ofstream(game / "OptiScaler" / "runtime.bin", std::ios::binary | std::ios::trunc) << "runtime-v2";
        std::ofstream(game / "OptiScaler" / "new-v2.bin", std::ios::binary) << "new-v2";
        std::ofstream(installed, std::ios::binary | std::ios::trunc) << "partial-state";
        std::ofstream(game / "NRFusion.install.ini", std::ios::binary | std::ios::trunc) << "marker-v2-partial";
        std::ofstream(state / "NRFusionProbe.exe", std::ios::binary | std::ios::trunc) << "probe-v2";
        std::ofstream(state / "dist.sha256", std::ios::binary | std::ios::trunc) << "dist-v2";
        std::ofstream(state / "Uninstall.exe", std::ios::binary | std::ios::trunc) << "uninstall-v2";

        st = InstallerState::RollbackTransaction(game, "dxgi.dll", installed, tx);
        assert(st && !fs::exists(tx));
        auto readAll = [](const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        assert(readAll(game / "dxgi.dll") == "proxy-v1");
        assert(readAll(game / "OptiScaler" / "runtime.bin") == "runtime-v1");
        assert(!fs::exists(game / "OptiScaler" / "new-v2.bin"));
        assert(readAll(installed) == installedV1);
        assert(readAll(game / "NRFusion.install.ini") == "marker-v1");
        assert(readAll(state / "NRFusionProbe.exe") == "probe-v1");
        assert(readAll(state / "dist.sha256") == "dist-v1");
        assert(readAll(state / "Uninstall.exe") == "uninstall-v1");

        // A committed transaction is cleanup-only even if its directory survived a filesystem error.
        st = InstallerState::SnapshotTransaction(game, "dxgi.dll", distV2, installed, tx);
        assert(st);
        st = InstallerState::CommitTransaction(tx);
        assert(st && !fs::exists(tx));
    }

    // Installer state rejects incomplete copies and never follows a managed symlink.
    {
        const fs::path game = root / "installer-hardening";
        const fs::path backup = game / "OptiScaler" / "NRFusion" / "backup";
        const fs::path manifest = root / "hardening-manifest.txt";
        const fs::path installed = game / "OptiScaler" / "NRFusion" / "installed.sha256";
        fs::create_directories(game);
        const std::string expected = "payload";
        const auto* pbytes = reinterpret_cast<const std::uint8_t*>(expected.data());
        const auto expectedHash = Sha256Hex(std::span<const std::uint8_t>(pbytes, expected.size()));
        std::ofstream(manifest) << expectedHash << "  OptiScaler.dll\n";

        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", manifest, backup);
        assert(st);
        st = InstallerState::RecordInstalled(game, "dxgi.dll", manifest, installed, backup);
        assert(!st && st.error.find("missing") != std::string::npos);

        // A broken symlink is still an existing filesystem object and must not be classified as absent.
        fs::remove_all(backup, ec);
        fs::create_symlink(game / "does-not-exist", game / "dxgi.dll", ec);
        if (!ec) {
            st = InstallerState::SnapshotExisting(game, "dxgi.dll", manifest, backup);
            assert(!st && st.error.find("not a regular file") != std::string::npos);
        }
        ec.clear();
    }

    // The manifest is an ownership contract. Duplicate logical names or two distribution entries
    // mapping to the same Windows install target must fail before any backup/deploy mutation.
    {
        const fs::path game = root / "installer-manifest-collisions";
        const fs::path backup = game / "backup";
        const fs::path duplicate = root / "duplicate-manifest.txt";
        const fs::path collision = root / "collision-manifest.txt";
        fs::create_directories(game);
        const std::string payload = "payload";
        const auto* pbytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const auto hash = Sha256Hex(std::span<const std::uint8_t>(pbytes, payload.size()));

        {
            std::ofstream m(duplicate);
            m << hash << "  OptiScaler/a.bin\n";
            m << hash << "  optiscaler/A.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", duplicate, backup);
        assert(!st && st.error.find("duplicate") != std::string::npos);
        assert(!fs::exists(backup));

        {
            std::ofstream m(collision);
            m << hash << "  OptiScaler.dll\n"; // maps to selected proxy name
            m << hash << "  dxgi.dll\n";       // maps to that same target directly
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", collision, backup);
        assert(!st && st.error.find("collide") != std::string::npos);
        assert(!fs::exists(backup));

        const fs::path windowsEscape = root / "windows-escape-manifest.txt";
        {
            std::ofstream m(windowsEscape);
            m << hash << "  ..\\escape.dll\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", windowsEscape, backup);
        assert(!st && st.error.find("unsafe") != std::string::npos);
        assert(!fs::exists(backup));

        const fs::path driveQualified = root / "drive-qualified-manifest.txt";
        {
            std::ofstream m(driveQualified);
            m << hash << "  C:escape.dll\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", driveQualified, backup);
        assert(!st && st.error.find("unsafe") != std::string::npos);
        assert(!fs::exists(backup));
    }

    // IntegratedCapabilities must track only what is actually wired into a real hook today: native
    // NGX calls and the stock D3D11-with-D3D12 bridge inside the shipped OptiScaler.dll for a game
    // with a real DLSS contract; EvaluateSynthetic/EvaluateSyntheticDx11/EvaluateSyntheticVk wired
    // into wrapped_swapchain.cpp's real Present hook for a game with none; and the x86 carrier
    // (nrfusion_capture32.dll + NRFusionHost64.exe, staged by tools/build_dist.ps1 and installed by
    // installer/NRFusion.nsi) for a 32-bit D3D11 game. Only OpenGL has no hook surface at all.
    {
        const auto integrated = GameProbe::IntegratedCapabilities();
        assert(integrated.nativeProvider && integrated.bridgeProvider &&
               integrated.syntheticD3D12 && integrated.syntheticD3D11Bridge &&
               integrated.syntheticVulkan && integrated.x86Carrier && !integrated.openGlCarrier);

        // 32-bit D3D11 without a native DLSS contract: the x86 carrier always runs its own
        // Synthetic pass regardless of native DLSS, so this is Supported once x86Carrier is wired.
        const auto dmc4seProbe = GameProbe::Probe(root / "dmc4se.exe");
        assert(GameProbe::InstallSupport(dmc4seProbe, integrated) == GameInstallSupport::Supported);

        // 64-bit D3D12 without a native DLSS contract: EvaluateSynthetic is wired into the real
        // Present hook, so this now clears through the Synthetic fallback.
        const auto nativeProbe = GameProbe::Probe(root / "native.exe");
        assert(GameProbe::InstallSupport(nativeProbe, integrated) == GameInstallSupport::Supported);

        // Same reasoning for a non-DLSS Vulkan game. The x86 carrier is D3D11-only, so a 32-bit
        // Vulkan game (unlike 32-bit D3D11) still fails closed even with x86Carrier wired.
        const auto vkProbe = GameProbe::Probe(root / "vk_game.exe");
        assert(GameProbe::InstallSupport(vkProbe, integrated) == GameInstallSupport::Supported);
        GameProbeResult vk32Probe = vkProbe;
        vk32Probe.bitness = 32;
        assert(GameProbe::InstallSupport(vk32Probe, integrated) == GameInstallSupport::Unsupported32BitApi);

        const auto legacyProbe = GameProbe::Probe(root / "legacy64.exe");
        assert(GameProbe::InstallSupport(legacyProbe, integrated) == GameInstallSupport::UnsupportedLegacyDirect3D);

        // OpenGL has no hook surface in this OptiScaler fork at all (no wglSwapBuffers hook), and
        // SyntheticOpenGlProvider is not even copied into the patched checkout.
        const auto glProbe = GameProbe::Probe(root / "gl_game.exe");
        assert(GameProbe::InstallSupport(glProbe, integrated) == GameInstallSupport::UnsupportedOpenGL);

        // 32-bit OpenGL: x86Carrier only covers D3D11, so this now clears the bitness gate and
        // reports the real reason (OpenGL, not 32-bit) instead of masking it.
        WriteFakePe(root / "gl_game32.exe", 32, "opengl32.dll wglCreateContext");
        const auto gl32Probe = GameProbe::Probe(root / "gl_game32.exe");
        assert(GameProbe::InstallSupport(gl32Probe, integrated) == GameInstallSupport::UnsupportedOpenGL);
    }

    fs::remove_all(root, ec);
    return 0;
}
