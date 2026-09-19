#include "nrfusion/GameProbe.hpp"
#include "nrfusion/InstallerState.hpp"
#include "nrfusion/Sha256.hpp"

#include <cstring>
#include <iostream>

namespace {
int ApiExitCode(nrfusion::GraphicsApi api) {
    switch (api) {
    case nrfusion::GraphicsApi::D3D9:
    case nrfusion::GraphicsApi::D3D10:
    case nrfusion::GraphicsApi::D3D11:
    case nrfusion::GraphicsApi::D3D12: return 10;
    case nrfusion::GraphicsApi::Vulkan: return 11;
    case nrfusion::GraphicsApi::OpenGL: return 12;
    default: return 1;
    }
}

int SupportExitCode(nrfusion::GameInstallSupport support) {
    switch (support) {
    case nrfusion::GameInstallSupport::Supported: return 0;
    case nrfusion::GameInstallSupport::Unsupported32Bit: return 20;
    case nrfusion::GameInstallSupport::UnsupportedOpenGL: return 21;
    case nrfusion::GameInstallSupport::UnsupportedLegacyDirect3D: return 23;
    case nrfusion::GameInstallSupport::ProviderUnavailable: return 24;
    case nrfusion::GameInstallSupport::Unsupported32BitApi: return 25;
    default: return 22;
    }
}

int PrintInstallerResult(const nrfusion::InstallerStateResult& r) {
    if (!r) {
        std::cerr << r.error;
        return 5;
    }
    std::cout << "considered=" << r.considered
              << " backed_up=" << r.backedUp
              << " recorded=" << r.recorded
              << " removed=" << r.removed
              << " restored=" << r.restored
              << " preserved_modified=" << r.preservedModified;
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--sha256") == 0) {
        const auto hash = nrfusion::Sha256File(argv[2]);
        if (!hash) return 3;
        std::cout << *hash;
        return 0;
    }
    if (argc == 4 && std::strcmp(argv[1], "--verify-sha256") == 0)
        return nrfusion::Sha256FileEquals(argv[2], argv[3]) ? 0 : 4;

    if (argc == 6 && std::strcmp(argv[1], "--snapshot-install") == 0)
        return PrintInstallerResult(nrfusion::InstallerState::SnapshotExisting(argv[2], argv[3], argv[4], argv[5]));
    if (argc == 7 && std::strcmp(argv[1], "--record-install") == 0)
        return PrintInstallerResult(nrfusion::InstallerState::RecordInstalled(argv[2], argv[3], argv[4], argv[5], argv[6]));
    if (argc == 6 && std::strcmp(argv[1], "--restore-install") == 0) {
        const auto r = nrfusion::InstallerState::RestoreOrRemove(argv[2], argv[3], argv[4], argv[5]);
        const int code = PrintInstallerResult(r);
        if (code != 0) return code;
        // Exit 6 means cleanup was safe but at least one user-modified file was deliberately preserved.
        // The NSIS uninstaller keeps its recovery state so the user can inspect/retry instead of losing backups.
        return r.preservedModified != 0 ? 6 : 0;
    }
    if (argc == 7 && std::strcmp(argv[1], "--snapshot-transaction") == 0)
        return PrintInstallerResult(nrfusion::InstallerState::SnapshotTransaction(argv[2], argv[3], argv[4], argv[5], argv[6]));
    if (argc == 6 && std::strcmp(argv[1], "--rollback-transaction") == 0)
        return PrintInstallerResult(nrfusion::InstallerState::RollbackTransaction(argv[2], argv[3], argv[4], argv[5]));
    if (argc == 3 && std::strcmp(argv[1], "--commit-transaction") == 0)
        return PrintInstallerResult(nrfusion::InstallerState::CommitTransaction(argv[2]));

    if (argc < 2 || argc > 3) {
        std::cerr << "usage: NRFusionProbe <game.exe> [--api-exit-code|--support-exit-code|--bitness-exit-code|--dlss-exit-code]\n"
                     "       NRFusionProbe --sha256 <file>\n"
                     "       NRFusionProbe --verify-sha256 <file> <sha256>\n"
                     "       NRFusionProbe --snapshot-install <game-dir> <proxy> <dist-manifest> <backup-dir>\n"
                     "       NRFusionProbe --record-install <game-dir> <proxy> <dist-manifest> <installed-manifest> <backup-dir>\n"
                     "       NRFusionProbe --restore-install <game-dir> <proxy> <installed-manifest> <backup-dir>\n"
                     "       NRFusionProbe --snapshot-transaction <game-dir> <proxy> <dist-manifest> <installed-manifest> <transaction-dir>\n"
                     "       NRFusionProbe --rollback-transaction <game-dir> <proxy> <installed-manifest> <transaction-dir>\n"
                     "       NRFusionProbe --commit-transaction <transaction-dir>\n";
        return 2;
    }
    const auto result = nrfusion::GameProbe::Probe(argv[1]);
    if (argc == 3 && std::strcmp(argv[2], "--api-exit-code") == 0)
        return ApiExitCode(result.api);
    if (argc == 3 && std::strcmp(argv[2], "--support-exit-code") == 0)
        return SupportExitCode(nrfusion::GameProbe::InstallSupport(result, nrfusion::GameProbe::IntegratedCapabilities()));
    if (argc == 3 && std::strcmp(argv[2], "--bitness-exit-code") == 0)
        return result.bitness;
    if (argc == 3 && std::strcmp(argv[2], "--dlss-exit-code") == 0)
        return result.HasNativeDlssContract() ? 1 : 0;

    std::cout << "bitness=" << result.bitness << "\n";
    std::cout << "api=" << nrfusion::GameProbe::ApiName(result.api) << "\n";
    std::cout << "score=" << result.apiScore << "\n";
    std::cout << "d3d_major=" << result.direct3DMajor << "\n";
    std::cout << "source=" << (result.apiFromSibling ? "sibling" : "executable") << "\n";
    std::cout << "evidence=" << result.evidenceFile.string() << "\n";
    std::cout << "dlss_sr=" << (result.hasDlssSr ? 1 : 0) << "\n";
    std::cout << "dlss_rr=" << (result.hasDlssRayReconstruction ? 1 : 0) << "\n";
    std::cout << "dlss_fg=" << (result.hasDlssFrameGeneration ? 1 : 0) << "\n";
    std::cout << "streamline=" << (result.hasStreamline ? 1 : 0) << "\n";
    std::cout << "reshade=" << (result.hasReShade ? 1 : 0) << "\n";
    std::cout << "optiscaler=" << (result.hasOptiScaler ? 1 : 0) << "\n";
    std::cout << "dxvk=" << (result.hasDxvk ? 1 : 0) << "\n";
    std::cout << "install_support=" << nrfusion::GameProbe::InstallSupportName(nrfusion::GameProbe::InstallSupport(result, nrfusion::GameProbe::IntegratedCapabilities())) << "\n";
    return result.api == nrfusion::GraphicsApi::Unknown ? 1 : 0;
}
