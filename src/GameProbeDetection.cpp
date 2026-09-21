#include "GameProbeInternal.hpp"

#include <algorithm>
#include <array>
#include <system_error>

namespace nrfusion::game_probe_detail {

int Hits(const std::string& data, std::initializer_list<std::pair<const char*, int>> needles) {
    int score = 0;
    for (const auto& [needle, weight] : needles)
        if (data.find(needle) != std::string::npos) score += weight;
    return score;
}

int DetectD3DMajor(const std::string& data) {
    if (data.find("d3d12createdevice") != std::string::npos || data.find("d3d12.dll") != std::string::npos) return 12;
    if (data.find("d3d11createdevice") != std::string::npos || data.find("d3d11.dll") != std::string::npos) return 11;
    if (data.find("d3d10_1.dll") != std::string::npos || data.find("d3d10.dll") != std::string::npos) return 10;
    if (data.find("direct3dcreate9") != std::string::npos || data.find("d3d9.dll") != std::string::npos) return 9;
    return 0;
}

GraphicsApi D3dApi(int major) noexcept {
    switch (major) {
    case 9: return GraphicsApi::D3D9;
    case 10: return GraphicsApi::D3D10;
    case 11: return GraphicsApi::D3D11;
    case 12: return GraphicsApi::D3D12;
    default: return GraphicsApi::Unknown;
    }
}

bool IsDirect3D(GraphicsApi api) noexcept {
    return api == GraphicsApi::D3D9 || api == GraphicsApi::D3D10 ||
           api == GraphicsApi::D3D11 || api == GraphicsApi::D3D12;
}

ApiScores ScoreApi(const std::string& data) {
    ApiScores s;
    s.d3d = Hits(data, {{"d3d12.dll", 5}, {"d3d12createdevice", 4}, {"d3d11.dll", 5},
                        {"d3d11createdevice", 4}, {"d3d10_1.dll", 5}, {"d3d10.dll", 4},
                        {"d3d9.dll", 3}, {"direct3dcreate9", 4}, {"dxgi.dll", 2}});
    s.vk = Hits(data, {{"vulkan-1.dll", 6}, {"vkcreateinstance", 4}, {"vkcreatedevice", 4},
                       {"vkqueuepresentkhr", 2}});
    s.gl = Hits(data, {{"opengl32.dll", 6}, {"wglcreatecontext", 4}, {"glgetstring", 2},
                       {"wglgetprocaddress", 2}});
    return s;
}

ApiPick PickApi(const ApiScores& s) {
    std::array<std::pair<ApiFamily, int>, 3> ranked {{{ApiFamily::Direct3D, s.d3d},
                                                       {ApiFamily::Vulkan, s.vk},
                                                       {ApiFamily::OpenGL, s.gl}}};
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (ranked[0].second < 4) return {};
    if (ranked[1].second > 0 && ranked[0].second - ranked[1].second < 3) return {};
    return {ranked[0].first, ranked[0].second};
}

GraphicsApi ResolveApi(ApiFamily family, const std::string& data) noexcept {
    if (family == ApiFamily::Vulkan) return GraphicsApi::Vulkan;
    if (family == ApiFamily::OpenGL) return GraphicsApi::OpenGL;
    if (family == ApiFamily::Direct3D) return D3dApi(DetectD3DMajor(data));
    return GraphicsApi::Unknown;
}

bool SkipSibling(const std::filesystem::path& path) {
    const std::string name = Lower(path.filename().string());
    static constexpr const char* exact[] = {
        "dxgi.dll", "d3d9.dll", "d3d10.dll", "d3d10_1.dll", "d3d11.dll", "d3d12.dll",
        "vulkan-1.dll", "opengl32.dll", "winmm.dll", "version.dll", "dbghelp.dll", "winhttp.dll", "wininet.dll"
    };
    for (const char* e : exact) if (name == e) return true;
    static constexpr const char* fragments[] = {
        "nvngx", "nvof", "reshade", "dlss5", "standalone-dlss", "dxvk", "dgvoodoo", "amd_", "intel_",
        "systemdetection", "crashreport", "crashpad", "easyanticheat", "battleye"
    };
    for (const char* f : fragments) if (name.find(f) != std::string::npos) return true;
    return false;
}

bool LooksLikeDxvkTransport(const std::filesystem::path& dir, std::filesystem::path* evidence) {
    static constexpr const char* candidates[] = {
        "dxgi.dll", "d3d11.dll", "d3d10core.dll", "d3d9.dll"
    };
    for (const char* name : candidates) {
        const auto path = dir / name;
        std::error_code ec;
        const auto status = std::filesystem::symlink_status(path, ec);
        if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) continue;
        const std::string data = ReadPrefix(path);
        if (data.empty()) continue;
        if (data.find("optiscaler") != std::string::npos) continue;
        const bool dxvk = data.find("dxvk") != std::string::npos;
        const bool vulkan = data.find("vulkan-1.dll") != std::string::npos ||
                            data.find("vkcreateinstance") != std::string::npos ||
                            data.find("vkcreatedevice") != std::string::npos;
        if (dxvk && vulkan) {
            if (evidence) *evidence = path;
            return true;
        }
    }
    return false;
}


void AddEvidence(GameProbeResult& result, const std::filesystem::path& path) {
    if (std::find(result.capabilityEvidence.begin(), result.capabilityEvidence.end(), path) ==
        result.capabilityEvidence.end())
        result.capabilityEvidence.push_back(path);
}

void ScanCapabilities(const std::filesystem::path& dir, GameProbeResult& result) {
    if (dir.empty()) return;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto path = entry.path();
        const std::string name = Lower(path.filename().string());

        if (name == "dxgi.dll") result.proxyDxgiOccupied = true;
        if (name == "version.dll") result.proxyVersionOccupied = true;
        if (name == "winmm.dll") result.proxyWinmmOccupied = true;

        const bool symlink = entry.is_symlink(ec);
        if (ec) { ec.clear(); continue; }
        const bool regular = entry.is_regular_file(ec);
        if (ec) { ec.clear(); continue; }
        if (!regular || symlink) continue;
        const bool dll = name.size() >= 4 && name.ends_with(".dll");

        if (name == "nvngx_dlss.dll") {
            result.hasDlssSr = true;
            result.hasNgxRuntime = true;
            AddEvidence(result, path);
        } else if (name == "nvngx_dlssd.dll") {
            result.hasDlssRayReconstruction = true;
            result.hasNgxRuntime = true;
            AddEvidence(result, path);
        } else if (name == "nvngx_dlssg.dll") {
            result.hasDlssFrameGeneration = true;
            result.hasNgxRuntime = true;
            AddEvidence(result, path);
        } else if (name.rfind("nvngx", 0) == 0 && dll) {
            result.hasNgxRuntime = true;
            AddEvidence(result, path);
        }

        if (name == "sl.interposer.dll" || name == "sl.common.dll" ||
            (name.rfind("sl.", 0) == 0 && dll)) {
            result.hasStreamline = true;
            AddEvidence(result, path);
        }
        if (name.find("reshade") != std::string::npos) {
            result.hasReShade = true;
            AddEvidence(result, path);
        }
        if (name.find("optiscaler") != std::string::npos) {
            result.hasOptiScaler = true;
            AddEvidence(result, path);
        }
        // Proxy DLLs often hide the implementation behind a generic filename. Inspect only the
        // common proxy candidates to avoid turning capability discovery into a full directory scan.
        if (name == "dxgi.dll" || name == "version.dll" || name == "winmm.dll" ||
            name == "d3d11.dll" || name == "d3d9.dll") {
            const std::string data = ReadPrefix(path);
            const bool isOpti = data.find("optiscaler") != std::string::npos;
            if (isOpti) {
                result.hasOptiScaler = true;
                AddEvidence(result, path);
            } else {
                if (data.find("reshade") != std::string::npos) {
                    result.hasReShade = true;
                    AddEvidence(result, path);
                }
                if (data.find("dxvk") != std::string::npos &&
                    (data.find("vulkan-1.dll") != std::string::npos ||
                     data.find("vkcreateinstance") != std::string::npos)) {
                    result.hasDxvk = true;
                    AddEvidence(result, path);
                }
            }
        }
        ec.clear();
    }
}

int EngineNameBonus(const std::filesystem::path& path, const std::filesystem::path& exe) {
    const std::string name = Lower(path.stem().string());
    const std::string base = Lower(exe.stem().string());
    int bonus = 0;
    if (!base.empty() && name.find(base) != std::string::npos) bonus += 3;
    for (const char* token : {"engine", "render", "renderer", "game", "client", "main", "core"})
        if (name.find(token) != std::string::npos) { bonus += 2; break; }
    return bonus;
}

} // namespace

} // namespace nrfusion::game_probe_detail
