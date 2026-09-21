#include "nrfusion/GameProbe.hpp"
#include "GameProbeInternal.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace nrfusion {

using namespace game_probe_detail;

GameProbeResult GameProbe::Probe(const std::filesystem::path& executable) {
    GameProbeResult result;
    const auto peInfo = InspectPe(executable);
    result.bitness = peInfo.bitness;

    // Direct executable imports outrank heuristics because they name the graphics runtime explicitly.
    for (const auto& dll : peInfo.importedDlls) {
        if (dll == "d3d12.dll") {
            result.api = GraphicsApi::D3D12;
            result.apiScore = 100;
            result.direct3DMajor = 12;
            result.evidenceFile = executable;
            break;
        }
        if (dll == "d3d11.dll") {
            result.api = GraphicsApi::D3D11;
            result.apiScore = 100;
            result.direct3DMajor = 11;
            result.evidenceFile = executable;
            break;
        }
        if (dll == "d3d10_1.dll" || dll == "d3d10.dll") {
            result.api = GraphicsApi::D3D10;
            result.apiScore = 100;
            result.direct3DMajor = 10;
            result.evidenceFile = executable;
            break;
        }
        if (dll == "d3d9.dll") {
            result.api = GraphicsApi::D3D9;
            result.apiScore = 100;
            result.direct3DMajor = 9;
            result.evidenceFile = executable;
            break;
        }
        if (dll == "vulkan-1.dll") {
            result.api = GraphicsApi::Vulkan;
            result.apiScore = 100;
            result.evidenceFile = executable;
            break;
        }
        if (dll == "opengl32.dll") {
            result.api = GraphicsApi::OpenGL;
            result.apiScore = 100;
            result.evidenceFile = executable;
            break;
        }
    }

    std::filesystem::path dxvkEvidence;
    const auto dir = executable.parent_path();
    ScanCapabilities(dir, result);
    if (!dir.empty() && LooksLikeDxvkTransport(dir, &dxvkEvidence)) {
        result.hasDxvk = true;
        AddEvidence(result, dxvkEvidence);
        result.api = GraphicsApi::Vulkan;
        result.apiScore = 100;
        result.apiFromSibling = true;
        result.evidenceFile = dxvkEvidence;
        return result;
    }

    if (result.api != GraphicsApi::Unknown) {
        return result;
    }

    const std::string ownData = ReadPrefix(executable);
    const auto ownPick = PickApi(ScoreApi(ownData));
    const auto ownApi = ResolveApi(ownPick.family, ownData);
    if (ownApi != GraphicsApi::Unknown && ownPick.score >= 6) {
        result.api = ownApi;
        result.apiScore = ownPick.score;
        result.direct3DMajor = IsDirect3D(ownApi) ? DetectD3DMajor(ownData) : 0;
        result.evidenceFile = executable;
        return result;
    }

    struct Candidate { ApiPick pick{}; GraphicsApi api = GraphicsApi::Unknown; int rank = 0; int direct3DMajor = 0; std::filesystem::path path; };
    Candidate best;
    std::error_code ec;
    std::size_t visited = 0;
    if (!dir.empty() && std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || visited >= kMaxSiblingCandidates) break;
            const bool symlink = entry.is_symlink(ec);
            if (ec) { ec.clear(); continue; }
            const bool regular = entry.is_regular_file(ec);
            if (ec) { ec.clear(); continue; }
            if (symlink || !regular) continue;
            const auto path = entry.path();
            if (std::filesystem::equivalent(path, executable, ec)) { ec.clear(); continue; }
            ec.clear();
            const auto ext = Lower(path.extension().string());
            if (ext != ".dll" && ext != ".exe") continue;
            if (SkipSibling(path)) continue;
            ++visited;

            const std::string candidateData = ReadPrefix(path);
            const ApiPick pick = PickApi(ScoreApi(candidateData));
            if (pick.family == ApiFamily::Unknown) continue;
            const GraphicsApi api = ResolveApi(pick.family, candidateData);
            if (api == GraphicsApi::Unknown) continue;
            const int rank = pick.score + EngineNameBonus(path, executable);
            const int d3dMajor = IsDirect3D(api) ? DetectD3DMajor(candidateData) : 0;
            if (rank > best.rank) best = {pick, api, rank, d3dMajor, path};
        }
    }

    if (best.api != GraphicsApi::Unknown) {
        result.api = best.api;
        result.apiScore = best.pick.score;
        result.direct3DMajor = best.direct3DMajor;
        result.apiFromSibling = true;
        result.evidenceFile = best.path;
    } else if (ownApi != GraphicsApi::Unknown) {
        result.api = ownApi;
        result.apiScore = ownPick.score;
        result.direct3DMajor = IsDirect3D(ownApi) ? DetectD3DMajor(ownData) : 0;
        result.evidenceFile = executable;
    }
    return result;
}

} // namespace nrfusion
