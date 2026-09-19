#pragma once

#include "nrfusion/RuntimeCapabilities.hpp"
#include "nrfusion/Types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nrfusion {

enum class GameInstallSupport : std::uint8_t {
    Supported,
    Unsupported32Bit,
    Unsupported32BitApi,
    UnsupportedOpenGL,
    UnsupportedLegacyDirect3D,
    ProviderUnavailable,
    Unknown
};

struct GameProbeResult {
    int bitness = 0; // 32 / 64 / 0 unknown
    GraphicsApi api = GraphicsApi::Unknown;
    int apiScore = 0;
    int direct3DMajor = 0; // 9/10/11/12 when the effective API is Direct3D
    bool apiFromSibling = false;
    std::filesystem::path evidenceFile;

    bool hasDlssSr = false;
    bool hasDlssRayReconstruction = false;
    bool hasDlssFrameGeneration = false;
    bool hasStreamline = false;
    bool hasReShade = false;
    bool hasOptiScaler = false;
    bool hasNgxRuntime = false;
    bool hasDxvk = false;

    bool proxyDxgiOccupied = false;
    bool proxyVersionOccupied = false;
    bool proxyWinmmOccupied = false;
    std::vector<std::filesystem::path> capabilityEvidence;

    bool HasNativeDlssContract() const noexcept {
        // FG presence alone does not prove that the game exposes the SR/RR frame contract NR needs.
        return hasDlssSr || hasDlssRayReconstruction;
    }
    GameContext ToGameContext() const noexcept {
        return GameContext{api, bitness == 32, HasNativeDlssContract(),
                           hasDlssRayReconstruction, hasDlssFrameGeneration};
    }
};

class GameProbe {
public:
    static GameProbeResult Probe(const std::filesystem::path& executable);
    static const char* ApiName(GraphicsApi api) noexcept;
    // Capabilities actually wired into the shipped OptiScaler.dll by apply_to_optiscaler.py --
    // not what the portable core merely has source for. Keep in sync with the real patcher.
    static RuntimeCapabilities IntegratedCapabilities() noexcept;
    static GameInstallSupport InstallSupport(const GameProbeResult& result) noexcept;
    static GameInstallSupport InstallSupport(const GameProbeResult& result,
                                             const RuntimeCapabilities& capabilities) noexcept;
    static const char* InstallSupportName(GameInstallSupport support) noexcept;
};

} // namespace nrfusion
