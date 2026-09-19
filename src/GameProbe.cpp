#include "nrfusion/GameProbe.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace nrfusion {
namespace {

constexpr std::uintmax_t kMaxScanBytes = 32ull * 1024ull * 1024ull;
constexpr std::size_t kMaxSiblingCandidates = 48;

enum class ApiFamily : std::uint8_t { Unknown, Direct3D, Vulkan, OpenGL };

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string ReadPrefix(const std::filesystem::path& path, std::uintmax_t limit = kMaxScanBytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    const auto wanted = static_cast<std::size_t>(std::min<std::uintmax_t>(ec ? limit : fileSize, limit));
    std::string data(wanted, '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return Lower(std::move(data));
}

struct PeInfo {
    int bitness = 0;
    std::vector<std::string> importedDlls;
};

PeInfo InspectPe(const std::filesystem::path& path) {
    PeInfo info;
    std::ifstream in(path, std::ios::binary);
    if (!in) return info;

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < 64) return info;

    std::array<unsigned char, 64> dos{};
    in.read(reinterpret_cast<char*>(dos.data()), static_cast<std::streamsize>(dos.size()));
    if (static_cast<std::size_t>(in.gcount()) < dos.size() || dos[0] != 'M' || dos[1] != 'Z') return info;

    const std::uint32_t pe = static_cast<std::uint32_t>(dos[0x3c]) |
                             (static_cast<std::uint32_t>(dos[0x3d]) << 8) |
                             (static_cast<std::uint32_t>(dos[0x3e]) << 16) |
                             (static_cast<std::uint32_t>(dos[0x3f]) << 24);

    if (pe > fileSize || fileSize - pe < 26) return info;
    in.clear();
    in.seekg(static_cast<std::streamoff>(pe), std::ios::beg);
    if (!in) return info;

    std::array<unsigned char, 26> peHeader{};
    in.read(reinterpret_cast<char*>(peHeader.data()), static_cast<std::streamsize>(peHeader.size()));
    if (static_cast<std::size_t>(in.gcount()) != peHeader.size() ||
        peHeader[0] != 'P' || peHeader[1] != 'E' || peHeader[2] != 0 || peHeader[3] != 0) return info;

    const std::uint16_t magic = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(peHeader[24]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(peHeader[25]) << 8));
    if (magic == 0x10b) info.bitness = 32;
    else if (magic == 0x20b) info.bitness = 64;
    else return info;

    const std::uint16_t numSections = static_cast<std::uint16_t>(peHeader[6]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(peHeader[7]) << 8);
    const std::uint16_t optHeaderSize = static_cast<std::uint16_t>(peHeader[20]) |
                                        static_cast<std::uint16_t>(static_cast<std::uint16_t>(peHeader[21]) << 8);

    if (numSections == 0 || optHeaderSize < 2 || fileSize - pe - 24 < optHeaderSize) return info;

    std::vector<unsigned char> optHeader(optHeaderSize);
    in.seekg(static_cast<std::streamoff>(pe + 24), std::ios::beg);
    in.read(reinterpret_cast<char*>(optHeader.data()), static_cast<std::streamsize>(optHeader.size()));
    if (static_cast<std::size_t>(in.gcount()) != optHeader.size()) return info;

    const std::size_t dataDirOffset = (info.bitness == 64) ? 112 : 96;
    if (optHeaderSize < dataDirOffset + 16) return info;

    std::uint32_t importRva = 0;
    std::uint32_t importSize = 0;
    std::memcpy(&importRva, &optHeader[dataDirOffset + 8], sizeof(importRva));
    std::memcpy(&importSize, &optHeader[dataDirOffset + 12], sizeof(importSize));
    if (importRva == 0 || importSize == 0) return info;

    struct Section {
        std::uint32_t virtualAddress = 0;
        std::uint32_t virtualSize = 0;
        std::uint32_t rawOffset = 0;
        std::uint32_t rawSize = 0;
    };
    std::vector<Section> sections(numSections);
    for (std::size_t i = 0; i < numSections; ++i) {
        std::array<unsigned char, 40> secHeader{};
        in.read(reinterpret_cast<char*>(secHeader.data()), static_cast<std::streamsize>(secHeader.size()));
        if (static_cast<std::size_t>(in.gcount()) != secHeader.size()) return info;

        std::memcpy(&sections[i].virtualSize, &secHeader[8], 4);
        std::memcpy(&sections[i].virtualAddress, &secHeader[12], 4);
        std::memcpy(&sections[i].rawSize, &secHeader[16], 4);
        std::memcpy(&sections[i].rawOffset, &secHeader[20], 4);
    }

    auto rvaToOffset = [&](std::uint32_t rva) -> std::uint64_t {
        for (const auto& sec : sections) {
            const auto extent = std::max(sec.virtualSize, sec.rawSize);
            if (rva >= sec.virtualAddress && rva < sec.virtualAddress + extent) {
                return static_cast<std::uint64_t>(sec.rawOffset) + (rva - sec.virtualAddress);
            }
        }
        return 0;
    };

    const auto importOffset = rvaToOffset(importRva);
    if (importOffset == 0 || importOffset >= fileSize) return info;

    in.seekg(static_cast<std::streamoff>(importOffset), std::ios::beg);

    constexpr std::size_t kMaxImportDescriptors = 256;
    for (std::size_t d = 0; d < kMaxImportDescriptors; ++d) {
        std::array<unsigned char, 20> desc{};
        in.read(reinterpret_cast<char*>(desc.data()), static_cast<std::streamsize>(desc.size()));
        if (static_cast<std::size_t>(in.gcount()) != desc.size()) break;
        bool allZero = true;
        for (unsigned char b : desc) { if (b != 0) { allZero = false; break; } }
        if (allZero) break;

        std::uint32_t nameRva = 0;
        std::memcpy(&nameRva, &desc[12], sizeof(nameRva));
        if (nameRva == 0) continue;

        const auto nameOffset = rvaToOffset(nameRva);
        if (nameOffset == 0 || nameOffset >= fileSize) continue;

        const auto savePos = in.tellg();
        in.seekg(static_cast<std::streamoff>(nameOffset), std::ios::beg);
        std::string dllName;
        char ch = '\0';
        while (in.get(ch) && ch != '\0' && dllName.size() < 128) {
            dllName.push_back(ch);
        }
        in.seekg(savePos);

        if (!dllName.empty()) {
            info.importedDlls.push_back(Lower(std::move(dllName)));
        }
    }

    return info;
}

int ReadPeBitness(const std::filesystem::path& path) {
    return InspectPe(path).bitness;
}

int Hits(const std::string& data, std::initializer_list<std::pair<const char*, int>> needles) {
    int score = 0;
    for (const auto& [needle, weight] : needles)
        if (data.find(needle) != std::string::npos) score += weight;
    return score;
}

struct ApiScores {
    int d3d = 0;
    int vk = 0;
    int gl = 0;
};

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

struct ApiPick { ApiFamily family = ApiFamily::Unknown; int score = 0; };

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

GameProbeResult GameProbe::Probe(const std::filesystem::path& executable) {
    GameProbeResult result;
    const auto peInfo = InspectPe(executable);
    result.bitness = peInfo.bitness;

    // Direct PE import analysis on the target executable:
    // When the executable explicitly links against a graphics runtime DLL, that is authoritative.
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
