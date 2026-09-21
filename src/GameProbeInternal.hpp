#pragma once

#include "nrfusion/GameProbe.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nrfusion::game_probe_detail {

inline constexpr std::size_t kMaxSiblingCandidates = 48;

enum class ApiFamily : std::uint8_t { Unknown, Direct3D, Vulkan, OpenGL };

struct PeInfo {
    int bitness = 0;
    std::vector<std::string> importedDlls;
};

struct ApiScores {
    int d3d = 0;
    int vk = 0;
    int gl = 0;
};

struct ApiPick {
    ApiFamily family = ApiFamily::Unknown;
    int score = 0;
};

std::string Lower(std::string value);
std::string ReadPrefix(const std::filesystem::path& path);
PeInfo InspectPe(const std::filesystem::path& path);

int DetectD3DMajor(const std::string& data);
bool IsDirect3D(GraphicsApi api) noexcept;
ApiScores ScoreApi(const std::string& data);
ApiPick PickApi(const ApiScores& scores);
GraphicsApi ResolveApi(ApiFamily family, const std::string& data) noexcept;
bool SkipSibling(const std::filesystem::path& path);
bool LooksLikeDxvkTransport(const std::filesystem::path& dir, std::filesystem::path* evidence);
void AddEvidence(GameProbeResult& result, const std::filesystem::path& path);
void ScanCapabilities(const std::filesystem::path& dir, GameProbeResult& result);
int EngineNameBonus(const std::filesystem::path& path, const std::filesystem::path& executable);

} // namespace nrfusion::game_probe_detail
