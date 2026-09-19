#include "nrfusion/AdaW4A8Interceptor.hpp"
#include "nrfusion/W4A8Ffn.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace nrfusion {
namespace {

// The swin kernels carry one argument, a struct, and each resolution level of the denoiser
// declares its own. The offsets below were read out of the shipped device code by
// tools/decode_swin_abi.py: each one is justified by an instruction in the kernel itself --
// a load address, a store address, a word polled beside a sleep, a word stored behind a fence.
//
// Reading all four with one fixed layout is what a 72-byte format would do, and it would hand
// the GPU whatever happens to sit at those offsets. The 32-channel level in particular is
// neither the same size nor the same shape as the other three.
struct SwinAbi {
    const char* family;
    std::uint32_t blockBytes;
    std::uint32_t input;       // read by every variant
    std::uint32_t output;      // written by every variant
    std::uint32_t chained;     // second buffer every variant reads
    std::uint32_t skipInput;   // read only by the upsampling variant
    std::uint32_t pooled;      // written only by the downsampling variant
    std::uint32_t waitFlag;    // polled next to a NANOSLEEP until the previous layer publishes
    std::uint32_t publish;     // stored behind a MEMBAR once this layer is done
    std::uint32_t extent;      // two int32 the kernel divides by four; kUnknownField where unread
};

constexpr std::uint32_t kUnknownField = 0xFFFFFFFFu;

// The 32-channel level is not the other three with padding: its skip input and its pooled output
// sit at different offsets, and its extent pair has not been identified.
constexpr SwinAbi kSwinAbi[] = {
    {"1h_32_1",  96, 0, 8, 16, 80, 64, 40, 56, kUnknownField},
    {"2h_64_2",  88, 0, 8, 16, 24, 72, 48, 64, 32},
    {"4h_128_4", 88, 0, 8, 16, 24, 72, 48, 64, 32},
    {"8h_256_8", 88, 0, 8, 16, 24, 72, 48, 64, 32},
};
constexpr int kSwinFamilies = static_cast<int>(sizeof(kSwinAbi) / sizeof(kSwinAbi[0]));

// The blocks that are 512 channels wide run their feed-forward as two launches: an expand that
// widens each of eight groups from 64 to 256 channels, and a project that narrows it back and
// folds in the skip. Those two are what the fused W4A8 kernel stands in for.
//
// The offsets were read out of the shipped device code by tools/decode_swin_abi.py and agree with
// a capture of a live frame: the project's skip argument holds the same address the expand read as
// its input, and the project waits on exactly the word the expand publishes.
enum FfwdStage { kExpandStage = 0, kProjectStage = 1, kForeignStage = 2 };

struct FfwdAbi {
    std::uint32_t blockBytes;
    std::uint32_t input;     // expand: the activations; project: the expand's output
    std::uint32_t skip;      // project only: the address the expand read, which is our input
    std::uint32_t output;
    std::uint32_t weights;   // constant for the life of the process, so it names the block
    std::uint32_t extent;    // two uint32 side by side
    std::uint32_t waitFlag;
    std::uint32_t publish;
};

constexpr std::uint32_t kNoField = 0xFFFFFFFFu;

constexpr FfwdAbi kFfwdAbi[2] = {
    /* expand  */ {56, 0, kNoField,  8, 16, 24, 32, 48},
    /* project */ {72, 0,        8, 16, 24, 32, 56, 48},
};

struct InterceptorState {
    std::recursive_mutex mutex;
    bool initialized = false;
    bool isAdaSm89 = false;
    bool isBlackwell = false;
    bool isAmpere = false;
    bool isTuring = false;
    std::string archName = "Unknown";
    bool assetsLoaded = false;
    bool enabled = true;

    std::string status = "Ada W4A8: standby";
    std::uint64_t interceptedLaunches = 0;
    std::uint64_t fallbackLaunches = 0;

    AdaW4A8ComputeMode configMode = AdaW4A8ComputeMode::Auto;
    bool forceBenchmark = false;
    bool sessionLocked = false;
    AdaW4A8ComputeMode lockedBackend = AdaW4A8ComputeMode::Dp4a;
    float lastDp4aUs = 0.0f;
    float lastTcUs = 0.0f;
    bool lastWasCached = false;
    std::string gpuId = "SM89";
    std::string exeName = "game.exe";
    fs::path cacheFilePath;

    std::uint64_t observedLaunches = 0;
    std::uint32_t lastParamSize = 0;
    std::uint64_t lastRejectedWindows = 0;
    std::uint32_t lastDims[2]{};
    // handle -> kExpandStage, kProjectStage or kForeignStage
    std::map<const void*, int> ffnKernels;
    std::uint64_t ffnWindowsAccepted = 0;

    std::map<const void*, int> swinKernels;   // handle -> index into kSwinAbi
    std::uint64_t swinLaunches[kSwinFamilies]{};
    std::uint32_t swinDims[kSwinFamilies][2]{};
    std::uint64_t swinBlockMismatches = 0;

    std::string statusScratch;

    // One container per block. They share a shape but not a single byte of weight, and the block a
    // launch belongs to is told apart by the weight address it carries.
    struct BlockWeights {
        WeightsSm89Header header{};
        std::vector<uint8_t> expWeights;
        std::vector<uint8_t> expScales;
        std::vector<uint16_t> expOutlierIndices;
        std::vector<uint8_t> expOutlierValues;
        std::vector<uint8_t> prjWeights;
        std::vector<uint8_t> prjScales;
        std::vector<uint16_t> prjOutlierIndices;
        std::vector<uint8_t> prjOutlierValues;
    };
    std::vector<BlockWeights> blocks;
    std::map<std::uint64_t, int> blockOfWeightAddress;   // runtime weight pointer -> index in blocks
    // The two stages of a block point at different weight arenas, so the expand cannot be
    // recognised by the address the project was bound with. This remembers which expand came
    // immediately before a project, which is enough to bind the expand's address to the same block.
    std::map<const void*, std::uint64_t> expandWeightsSeen;
    // What a suppressed expand would have published. If the substitution then cannot run,
    // this is the word that has to be written anyway, or the model waits on it forever.
    struct PendingPublish { std::uint64_t flag = 0; std::uint32_t blocks = 0; };
    std::map<const void*, PendingPublish> suppressedExpandPublish;
    int boundBlocks = 0;   // two addresses map to each block, so the map size is not this
    std::uint64_t ffwdExpandLaunches = 0;
    std::uint64_t ffwdProjectLaunches = 0;
    std::uint64_t ffwdUnbound = 0;
    std::uint64_t ffwdBlockMismatches = 0;
    std::uint64_t ffwdExpandSuppressed = 0;
    std::uint64_t ffwdRescuedPublishes = 0;
    std::vector<bool> blockReplaced;   // this block's project has been replaced at least once
    bool pairingProven = false;

    fs::path moduleDir;
};

bool TryReadCache(const fs::path& cacheFile, const std::string& key, AdaW4A8ComputeMode& outMode) {
    if (cacheFile.empty() || !fs::exists(cacheFile)) return false;
    char buf[128]{};
    if (GetPrivateProfileStringA("AdaW4A8Cache", key.c_str(), "", buf, sizeof(buf), cacheFile.string().c_str()) > 0) {
        std::string val(buf);
        for (char& c : val) c = static_cast<char>(std::tolower(c));
        if (val.find("tensor") != std::string::npos) {
            outMode = AdaW4A8ComputeMode::TensorCore;
            return true;
        } else if (val.find("dp4a") != std::string::npos) {
            outMode = AdaW4A8ComputeMode::Dp4a;
            return true;
        }
    }
    return false;
}

void WriteCache(const fs::path& cacheFile, const std::string& key, AdaW4A8ComputeMode mode, float dp4aUs, float tcUs) {
    if (cacheFile.empty()) return;
    char val[128]{};
    const char* modeStr = (mode == AdaW4A8ComputeMode::TensorCore) ? "TensorCore" : "DP4A";
    std::snprintf(val, sizeof(val), "%s ; DP4A=%.1fus, TensorCore=%.1fus", modeStr, dp4aUs, tcUs);
    WritePrivateProfileStringA("AdaW4A8Cache", key.c_str(), val, cacheFile.string().c_str());
}

bool SafeCopyParams(void* dest, const void* src, size_t bytes) noexcept {
    if (!dest || !src || bytes == 0) return false;
#if defined(_MSC_VER)
    __try {
        std::memcpy(dest, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    std::memcpy(dest, src, bytes);
    return true;
#endif
}

// The expand is suppressed on the promise that the substitution will run. When that promise cannot
// be kept, it still has to be paid: the word the expand would have published gets written by a
// launch of its own, so whatever waits on it proceeds instead of sleeping forever.
void RescueSuppressedPublish(InterceptorState& state, ID3D12GraphicsCommandList* cmdList);

// Replacing a kernel the model still needs would corrupt the frame, so the substitution only runs
// when it is asked for by name. An environment variable alone cannot carry it: the Ubisoft launcher
// respawns the game and the child never inherits one.
bool ArmedForAdaW4A8(const InterceptorState& state) {
    static const bool armed = [&]{
        char value[16]{};
        if (GetEnvironmentVariableA("NRFUSION_ADA_W4A8_ARM", value, sizeof(value)) > 0) return true;
        for (const auto& ini : { state.moduleDir / "OptiScaler.ini", fs::current_path() / "OptiScaler.ini" }) {
            if (!fs::exists(ini)) continue;
            char setting[16]{};
            if (GetPrivateProfileStringA("DlssNr", "AdaW4A8Arm", "", setting, sizeof(setting), ini.string().c_str()) > 0) {
                std::string text(setting);
                for (char& c : text) c = static_cast<char>(std::tolower(c));
                return text == "true" || text == "1" || text == "yes" || text == "on";
            }
        }
        return false;
    }();
    return armed;
}

InterceptorState& State() {
    static InterceptorState state;
    return state;
}

// Arming every block at once turns a single wrong assumption into a hung device with nothing to
// bisect. This narrows the substitution to one block index, so a failure names the block it came
// from; unset, every block for which a container exists is armed.
int ArmedBlockIndex() {
    static const int only = []{
        char value[16]{};
        if (GetEnvironmentVariableA("NRFUSION_ADA_W4A8_ARM_BLOCK", value, sizeof(value)) > 0)
            return std::atoi(value);
        auto& state = State();
        for (const auto& ini : { state.moduleDir / "OptiScaler.ini", fs::current_path() / "OptiScaler.ini" }) {
            if (!fs::exists(ini)) continue;
            char setting[16]{};
            if (GetPrivateProfileStringA("DlssNr", "AdaW4A8ArmBlock", "", setting, sizeof(setting),
                                         ini.string().c_str()) > 0)
                return std::atoi(setting);
        }
        return -1;
    }();
    return only;
}

AdaW4A8CubinLauncher g_cubinLauncher = nullptr;

void RescueSuppressedPublish(InterceptorState& state, ID3D12GraphicsCommandList* cmdList) {
    InterceptorState::PendingPublish owed;
    {
        std::lock_guard<std::recursive_mutex> lock(state.mutex);
        const auto pending = state.suppressedExpandPublish.find(cmdList);
        if (pending == state.suppressedExpandPublish.end() || !pending->second.flag) return;
        owed = pending->second;
        state.suppressedExpandPublish.erase(pending);
        ++state.ffwdRescuedPublishes;
    }
    if (g_cubinLauncher == nullptr) return;
    AdaW4A8Params publish{};
    publish.windows = 0;   // the launcher reads this as: fill the flag slots, run nothing
    publish.publishFlag = reinterpret_cast<void*>(owed.flag);
    publish.publisherBlocks = owed.blocks;
    g_cubinLauncher(cmdList, publish);
}

// "active" must mean a kernel ran, not that the hardware matched.
std::string ReadyStatus(const char* backend) {
    std::string text = std::string("Ada W4A8 ready (") + backend + ")";
    if (g_cubinLauncher == nullptr)
        text += " -- inert: this build installed no cubin launcher";
    return text;
}

fs::path GetCurrentModuleDirectory() {
    wchar_t path[32768]{};
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&GetCurrentModuleDirectory), &module) &&
        GetModuleFileNameW(module, path, 32768)) {
        return fs::path(path).parent_path();
    }
    return fs::current_path();
}

} // namespace

bool IsAdaSm89Architecture(ID3D12Device* device) noexcept {
    (void)device;
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.initialized) {
        return s.isAdaSm89;
    }

    s.initialized = true;
    s.isAdaSm89 = false;
    s.isBlackwell = false;
    s.moduleDir = GetCurrentModuleDirectory();

    wchar_t exePathW[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePathW, MAX_PATH)) {
        s.exeName = fs::path(exePathW).filename().string();
    } else {
        s.exeName = "game.exe";
    }
    s.cacheFilePath = s.moduleDir / "nrfusion_ada_cache.ini";

    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(DISPLAY_DEVICEW);

    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        std::wstring devId(dd.DeviceID);
        std::wstring devString(dd.DeviceString);

        // Check if vendor is NVIDIA (VEN_10DE)
        auto venPos = devId.find(L"VEN_10DE");
        if (venPos != std::wstring::npos) {
            uint32_t deviceId = 0;
            auto devPos = devId.find(L"DEV_", venPos);
            if (devPos != std::wstring::npos && devPos + 8 <= devId.size()) {
                std::wstring devHex = devId.substr(devPos + 4, 4);
                try {
                    deviceId = static_cast<uint32_t>(std::stoul(devHex, nullptr, 16));
                } catch (...) {}
            }

            // Check for Blackwell (RTX 50 series):
            if (devString.find(L"RTX 50") != std::wstring::npos ||
                devString.find(L"Blackwell") != std::wstring::npos ||
                (deviceId >= 0x2900 && deviceId <= 0x2BFF)) {
                s.isBlackwell = true;
                s.isAdaSm89 = false;
                s.archName = "Blackwell (RTX 50)";
                s.status = "Blackwell RTX 50 detected: using existing default path untouched";
                return false;
            }

            // Check for Ada Lovelace (RTX 40 series):
            if (devString.find(L"RTX 40") != std::wstring::npos ||
                devString.find(L"Ada") != std::wstring::npos ||
                (deviceId >= 0x2600 && deviceId <= 0x28FF)) {
                s.isAdaSm89 = true;
                s.archName = "Ada Lovelace (RTX 40)";

                char devBuf[32]{};
                std::snprintf(devBuf, sizeof(devBuf), "DEV_%04X", deviceId);
                s.gpuId = devBuf;
                s.status = "Ada Lovelace RTX 40 (SM89) detected: Ada W4A8 path eligible";
                return true;
            }

            // Check for Ampere (RTX 30 series):
            if (devString.find(L"RTX 30") != std::wstring::npos ||
                devString.find(L"Ampere") != std::wstring::npos ||
                (deviceId >= 0x2200 && deviceId <= 0x25FF)) {
                s.isAmpere = true;
                s.archName = "Ampere (RTX 30)";
                s.status = "Ampere RTX 30 detected: using standard DLSS-NR baseline";
                return false;
            }

            // Check for Turing (RTX 20 / GTX 16 series):
            if (devString.find(L"RTX 20") != std::wstring::npos ||
                devString.find(L"GTX 16") != std::wstring::npos ||
                devString.find(L"Turing") != std::wstring::npos ||
                (deviceId >= 0x1E80 && deviceId <= 0x21FF)) {
                s.isTuring = true;
                s.archName = "Turing (RTX 20 / GTX 16)";
                s.status = "Turing RTX 20 detected: using standard DLSS-NR baseline";
                return false;
            }

            s.archName = "NVIDIA";
            s.status = "NVIDIA GPU detected: using standard DLSS-NR baseline";
            return false;
        }
    }

    s.status = "Ada W4A8: No Ada SM89 GPU detected";
    return false;
}

bool InitializeAdaW4A8(ID3D12Device* device) noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);

    if (s.assetsLoaded) return true;
    if (!IsAdaSm89Architecture(device)) return false;

    // One container per block, all of them next to the module. A single file was enough while the
    // path targeted one block; it is not enough now that sixteen blocks share the shape and each
    // one has its own weights.
    const std::vector<fs::path> searchDirs = {
        s.moduleDir / "w4a8",
        s.moduleDir / "OptiScaler" / "w4a8",
        s.moduleDir,
        s.moduleDir / "OptiScaler",
        s.moduleDir / "OptiScaler" / "nvfp4" / "hybrid",
        fs::current_path() / "w4a8",
        fs::current_path() / "dist" / "w4a8",
        fs::current_path(),
        fs::current_path() / "data" / "pesos",
    };

    const auto loadContainer = [&](const fs::path& path) -> bool {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        InterceptorState::BlockWeights block;
        file.read(reinterpret_cast<char*>(&block.header), sizeof(block.header));
        if (!file || block.header.magic != 0x39384D53) return false;

        const auto readChunk = [&](auto& target, std::uint32_t offset, std::uint32_t bytes) {
            target.resize(bytes / sizeof(typename std::remove_reference_t<decltype(target)>::value_type));
            file.seekg(offset);
            file.read(reinterpret_cast<char*>(target.data()), bytes);
        };
        readChunk(block.expWeights, block.header.expandWeightsInt4Offset, block.header.expandWeightsInt4Size);
        readChunk(block.expScales, block.header.expandScalesOffset, block.header.expandScalesSize);
        readChunk(block.expOutlierIndices, block.header.expandOutlierIndicesOffset, block.header.expandOutlierIndicesSize);
        readChunk(block.expOutlierValues, block.header.expandOutlierValuesOffset, block.header.expandOutlierValuesSize);
        readChunk(block.prjWeights, block.header.projectWeightsInt4Offset, block.header.projectWeightsInt4Size);
        readChunk(block.prjScales, block.header.projectScalesOffset, block.header.projectScalesSize);
        readChunk(block.prjOutlierIndices, block.header.projectOutlierIndicesOffset, block.header.projectOutlierIndicesSize);
        readChunk(block.prjOutlierValues, block.header.projectOutlierValuesOffset, block.header.projectOutlierValuesSize);
        if (!file) return false;
        s.blocks.push_back(std::move(block));
        return true;
    };

    for (const auto& dir : searchDirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("weights_sm89", 0) != 0 || entry.path().extension() != ".bin") continue;
            loadContainer(entry.path());
        }
        if (!s.blocks.empty()) break;
    }

    if (s.blocks.empty()) {
        s.status = "Ada W4A8 fallback: no weights_sm89*.bin container found";
        return false;
    }

    // Ordered by block index so that binding a launch to a container never depends on the order the
    // file system happened to hand the files back.
    std::sort(s.blocks.begin(), s.blocks.end(),
              [](const auto& a, const auto& b) { return a.header.blockIndex < b.header.blockIndex; });
    s.blockReplaced.assign(s.blocks.size(), false);

    try {
        // Backend selection configuration (Auto by default, with DP4A and TensorCore manual overrides)
        s.configMode = AdaW4A8ComputeMode::Auto;
        s.forceBenchmark = false;

        char envBuf[64]{};
        if (GetEnvironmentVariableA("NRFUSION_FORCE_BENCHMARK", envBuf, sizeof(envBuf)) > 0) {
            s.forceBenchmark = (envBuf[0] == '1' || envBuf[0] == 't' || envBuf[0] == 'T');
        }

        if (GetEnvironmentVariableA("NRFUSION_ADA_BACKEND", envBuf, sizeof(envBuf)) > 0 ||
            GetEnvironmentVariableA("NRFUSION_SM89_W4A8_MODE", envBuf, sizeof(envBuf)) > 0 ||
            GetEnvironmentVariableA("NRFUSION_ADA_W4A8_MODE", envBuf, sizeof(envBuf)) > 0) {
            std::string m(envBuf);
            for (char& c : m) c = static_cast<char>(std::tolower(c));
            if (m == "dp4a" || m == "alu" || m == "cuda") {
                s.configMode = AdaW4A8ComputeMode::Dp4a;
            } else if (m == "tensor" || m == "tc" || m == "wmma" || m == "tensorcore") {
                s.configMode = AdaW4A8ComputeMode::TensorCore;
            } else {
                s.configMode = AdaW4A8ComputeMode::Auto;
            }
        } else {
            // Check OptiScaler.ini [DlssNr] or [AdaW4A8] Backend / W4A8Mode / ComputeMode
            const std::vector<fs::path> iniCandidates = {
                s.moduleDir / "OptiScaler.ini",
                fs::current_path() / "OptiScaler.ini",
            };
            for (const auto& iniPath : iniCandidates) {
                if (fs::exists(iniPath)) {
                    char iniBuf[64]{};
                    if (GetPrivateProfileStringA("DlssNr", "Backend", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("DlssNr", "W4A8Mode", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("DlssNr", "ComputeMode", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("AdaW4A8", "Backend", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0) {
                        std::string m(iniBuf);
                        for (char& c : m) c = static_cast<char>(std::tolower(c));
                        if (m == "dp4a" || m == "alu" || m == "cuda") {
                            s.configMode = AdaW4A8ComputeMode::Dp4a;
                        } else if (m == "tensor" || m == "tc" || m == "wmma" || m == "tensorcore") {
                            s.configMode = AdaW4A8ComputeMode::TensorCore;
                        } else {
                            s.configMode = AdaW4A8ComputeMode::Auto;
                        }
                    }
                    if (GetPrivateProfileStringA("DlssNr", "AdaW4A8", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("DlssNr", "W4A8", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("AdaW4A8", "Enabled", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0) {
                        std::string w4(iniBuf);
                        for (char& c : w4) c = static_cast<char>(std::tolower(c));
                        s.enabled = !(w4 == "false" || w4 == "0" || w4 == "no" || w4 == "off");
                    }
                    if (GetPrivateProfileStringA("DlssNr", "RecalibrateBackend", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("DlssNr", "ForceBenchmark", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("AdaW4A8", "RecalibrateBackend", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0 ||
                        GetPrivateProfileStringA("AdaW4A8", "ForceBenchmark", "", iniBuf, sizeof(iniBuf), iniPath.string().c_str()) > 0) {
                        std::string fb(iniBuf);
                        for (char& c : fb) c = static_cast<char>(std::tolower(c));
                        s.forceBenchmark = (fb == "true" || fb == "1" || fb == "yes");
                    }
                    break;
                }
            }
        }

        if (!s.enabled) {
            s.status = "Ada W4A8: bypassed (standard NVIDIA FP8 active)";
        } else if (s.configMode == AdaW4A8ComputeMode::Dp4a) {
            s.lockedBackend = AdaW4A8ComputeMode::Dp4a;
            s.sessionLocked = true;
            s.status = ReadyStatus("CUDA Core dp4a [manual override]");
        } else if (s.configMode == AdaW4A8ComputeMode::TensorCore) {
            s.lockedBackend = AdaW4A8ComputeMode::TensorCore;
            s.sessionLocked = true;
            s.status = ReadyStatus("INT8 Tensor Core [manual override]");
        } else {
            // Auto mode: unlock to calibrate/load on first launch
            s.sessionLocked = false;
            s.status = ReadyStatus("Auto: awaiting first launch");
        }

        s.assetsLoaded = true;
        return true;
    } catch (const std::exception& e) {
        s.status = std::string("Ada W4A8 fallback: ") + e.what();
        return false;
    }
}

bool TryInterceptAdaW4A8(ID3D12GraphicsCommandList* cmdList, const void* launchParams, std::uint32_t count) noexcept {
    auto& s = State();

    if (!cmdList || !launchParams || count != 1) {
        return false;
    }

    if (!s.enabled) {
        return false;
    }

    // Rule: RTX 50 (Blackwell) must use existing default path untouched
    if (!IsAdaSm89Architecture(nullptr)) {
        return false;
    }

    if (!InitializeAdaW4A8(nullptr)) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }

    // The kernel launch parameters ABI from NVAPI (NVAPI_CU_KERNEL_LAUNCH_PARAMS)
    struct LaunchParamsLayout {
        void* hFunction;
        struct { unsigned x, y, z; } gridDim;
        struct { unsigned x, y, z; } blockDim;
        unsigned dynSharedMemBytes;
        const void* pParams;
        unsigned paramSize;
    };

    const auto* k = static_cast<const LaunchParamsLayout*>(launchParams);
    int ffwdStage = -1;
    int swinFamily = -1;
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.observedLaunches;
        if (k) s.lastParamSize = k->paramSize;
        if (k) {
            const auto known = s.ffnKernels.find(k->hFunction);
            if (known != s.ffnKernels.end()) ffwdStage = known->second;
            const auto swin = s.swinKernels.find(k->hFunction);
            if (swin != s.swinKernels.end()) swinFamily = swin->second;
        }
    }

    // A swin layer is one fused launch, not the pair the feed-forward block runs, so nothing here
    // is held back waiting for a partner. Until a kernel and a weight container exist for the
    // shape, the only honest thing to do with the launch is read it and let it run.
    if (swinFamily >= 0 && k && k->pParams) {
        const SwinAbi& abi = kSwinAbi[swinFamily];
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.swinLaunches[swinFamily];
        unsigned extent[2]{};
        if (k->paramSize != abi.blockBytes) {
            ++s.swinBlockMismatches;
        } else if (abi.extent != kUnknownField &&
                   SafeCopyParams(extent, static_cast<const char*>(k->pParams) + abi.extent, sizeof(extent))) {
            s.swinDims[swinFamily][0] = extent[0];
            s.swinDims[swinFamily][1] = extent[1];
        }
    }
    if (swinFamily >= 0) return false;

    if (ffwdStage < 0 || !k || !k->pParams) return false;

    // A kernel of another family carrying the same argument size is not this one. Naming it and
    // then refusing it is what keeps the earlier mistake -- feeding one family's weights to
    // another family's kernel -- from being possible at all.
    if (ffwdStage == kForeignStage) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }

    const FfwdAbi& abi = kFfwdAbi[ffwdStage];
    if (k->paramSize != abi.blockBytes) {
        // Named correctly but laid out differently: reading it by these offsets would hand the GPU
        // whatever happens to sit at them.
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.ffwdBlockMismatches;
        return false;
    }

    std::uint64_t weightsAddress = 0;
    unsigned extent[2]{};
    if (!SafeCopyParams(&weightsAddress, static_cast<const char*>(k->pParams) + abi.weights, sizeof(weightsAddress)) ||
        !SafeCopyParams(extent, static_cast<const char*>(k->pParams) + abi.extent, sizeof(extent))) {
        return false;
    }

    // The weight address never moves for the life of the process, so it is the one thing in the
    // launch that says which block this is. Containers are bound to it in the order the blocks are
    // first seen, which is the order the graph evaluates them.
    int blockSlot = -1;
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (ffwdStage == kExpandStage) {
            ++s.ffwdExpandLaunches;
            s.expandWeightsSeen[cmdList] = weightsAddress;
        } else {
            ++s.ffwdProjectLaunches;
        }
        auto bound = s.blockOfWeightAddress.find(weightsAddress);
        if (bound != s.blockOfWeightAddress.end()) {
            blockSlot = bound->second;
        } else if (ffwdStage == kProjectStage) {
            // Bound by order of first appearance, which is the order the graph evaluates the
            // blocks, and only while there is a container left to bind.
            const int next = s.boundBlocks;
            if (next < static_cast<int>(s.blocks.size())) {
                s.blockOfWeightAddress[weightsAddress] = next;
                blockSlot = next;
                ++s.boundBlocks;
                const auto expand = s.expandWeightsSeen.find(cmdList);
                if (expand != s.expandWeightsSeen.end())
                    s.blockOfWeightAddress[expand->second] = next;
            } else {
                ++s.ffwdUnbound;   // more distinct blocks in the graph than containers shipped
            }
        }
    }

    // Letting the expand run and replacing only the project costs more than it saves: the fused
    // kernel redoes the expand, so the work is paid twice. Suppressing it only becomes safe once a
    // whole evaluation has been watched and every block seen has had its project follow -- until
    // then the expand runs and the counters are what prove the pairing.
    if (ffwdStage == kExpandStage) {
        std::uint64_t expandPublish = 0;
        if (!SafeCopyParams(&expandPublish, static_cast<const char*>(k->pParams) + abi.publish,
                            sizeof(expandPublish))) {
            return false;
        }
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        const bool paired = s.pairingProven && blockSlot >= 0 && s.blockReplaced[blockSlot] &&
                            ArmedForAdaW4A8(s);
        if (!paired) return false;
        s.suppressedExpandPublish[cmdList] = {expandPublish,
                                              k->gridDim.x * k->gridDim.y * k->gridDim.z};
        ++s.ffwdExpandSuppressed;
        return true;
    }
    // Every way out from here has to pay the promise a suppressed expand left behind.
    if (blockSlot < 0) {
        RescueSuppressedPublish(s, cmdList);
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }

    std::uint64_t inputAddress = 0;
    std::uint64_t outputAddress = 0;
    std::uint64_t publishAddress = 0;
    if (!SafeCopyParams(&inputAddress, static_cast<const char*>(k->pParams) + abi.skip, sizeof(inputAddress)) ||
        !SafeCopyParams(&outputAddress, static_cast<const char*>(k->pParams) + abi.output, sizeof(outputAddress)) ||
        !SafeCopyParams(&publishAddress, static_cast<const char*>(k->pParams) + abi.publish, sizeof(publishAddress)) ||
        !inputAddress || !outputAddress) {
        RescueSuppressedPublish(s, cmdList);
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }

    // The pair of integers counts shifted windows, not tokens: a window is 8 by 8 pixels, which is
    // the 64 tokens the kernel consumes at a time. A frame measured on this runtime carries 36 by
    // 60 windows here and 72 by 120 one resolution level up, which is the same surface halved.
    //
    // Reading them as a token count -- the contract of the other family this path used to target --
    // would reject every launch, because 36 times 60 is not a multiple of 64.
    const uint64_t windows = static_cast<uint64_t>(extent[0]) * extent[1];
    {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (windows) s.ffnWindowsAccepted = windows;
    }
    if (windows == 0 || windows > 65535) {
        RescueSuppressedPublish(s, cmdList);
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        s.lastRejectedWindows = windows;
        s.lastDims[0] = extent[0];
        s.lastDims[1] = extent[1];
        return false;
    }

    {
        // Every container has been matched to a block and each of those blocks has now been seen
        // projecting after expanding. Only from here is suppressing the expand a safe trade.
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.pairingProven &&
            s.boundBlocks == static_cast<int>(s.blocks.size()) &&
            s.ffwdExpandLaunches >= s.blocks.size() &&
            s.ffwdProjectLaunches >= s.blocks.size()) {
            s.pairingProven = true;
        }
    }

    const InterceptorState::BlockWeights& weights = s.blocks[blockSlot];
    // The kernel folds a whole 64-channel scale group into one accumulator, so a container
    // quantized at another group size would be read with the wrong scales.
    if (weights.header.groupSize != 64) {
        RescueSuppressedPublish(s, cmdList);
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }
    if (ArmedBlockIndex() >= 0 && static_cast<int>(weights.header.blockIndex) != ArmedBlockIndex()) {
        RescueSuppressedPublish(s, cmdList);
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }

    // Prepare our AdaW4A8Params contract
    AdaW4A8Params adaParams{};
    adaParams.input = reinterpret_cast<const void*>(inputAddress);
    adaParams.output = reinterpret_cast<void*>(outputAddress);
    adaParams.firstProjection = nullptr;
    adaParams.publishFlag = reinterpret_cast<void*>(publishAddress);
    adaParams.publisherBlocks = k->gridDim.x * k->gridDim.y * k->gridDim.z;
    // The stage being replaced reads and writes E4M3; every store in its own code passes
    // through a saturating conversion to that format before it lands.
    adaParams.activationFormat = AdaW4A8Params::ActivationE4M3;

    adaParams.expandWeightsInt4 = weights.expWeights.data();

    adaParams.expandScales = weights.expScales.data();
    adaParams.expandOutlierIndices = weights.expOutlierIndices.data();
    adaParams.expandOutlierValues = weights.expOutlierValues.data();
    adaParams.expandOutliersTotal = weights.header.expandOutliersTotal;

    adaParams.projectWeightsInt4 = weights.prjWeights.data();
    adaParams.projectScales = weights.prjScales.data();
    adaParams.projectOutlierIndices = weights.prjOutlierIndices.data();
    adaParams.projectOutlierValues = weights.prjOutlierValues.data();
    adaParams.projectOutliersTotal = weights.header.projectOutliersTotal;

    adaParams.tokens = 64;
    adaParams.groups = weights.header.groups;
    adaParams.groupChannels = weights.header.groupChannels;
    adaParams.wideChannels = weights.header.wideChannels;
    adaParams.groupSize = weights.header.groupSize;
    adaParams.channels = weights.header.groups * weights.header.groupChannels;
    adaParams.blockIndex = weights.header.blockIndex;
    // Version 2 containers store the weights at the width the tensor core already uses.
    adaParams.weightBits = (weights.header.version >= 2) ? 8 : 4;
    adaParams.windows = static_cast<uint32_t>(windows);
    adaParams.stream = nullptr;

    // The A/B benchmark measures through the CUDA runtime, which the cubin route does not use.
    // Ada's measured winner is the Tensor Core path; a failure there still falls back to DP4A below.
    const bool useCubin = (g_cubinLauncher != nullptr);
    if (useCubin && !s.sessionLocked) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (s.configMode == AdaW4A8ComputeMode::Auto) s.lockedBackend = AdaW4A8ComputeMode::TensorCore;
        s.sessionLocked = true;
    }

    // Handle Auto mode selection, cache lookup and session lock
    if (!useCubin && !s.sessionLocked) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.sessionLocked) {
            const std::string cacheKey = s.gpuId + "_" + s.exeName + "_b" +
                                         std::to_string(weights.header.blockIndex) + "_t" +
                                         std::to_string(windows) + "_v" +
                                         std::to_string(weights.header.version);

            AdaW4A8ComputeMode cachedChoice = AdaW4A8ComputeMode::Dp4a;
            if (!s.forceBenchmark && TryReadCache(s.cacheFilePath, cacheKey, cachedChoice)) {
                s.lockedBackend = cachedChoice;
                s.sessionLocked = true;
                s.lastWasCached = true;
                const char* choiceStr = (s.lockedBackend == AdaW4A8ComputeMode::TensorCore) ? "TensorCore" : "DP4A";
                std::fprintf(stderr, "\nNRFusion Ada Auto\nSelected: %s (cached)\n\n", choiceStr);
                std::fflush(stderr);
            } else {
                s.forceBenchmark = false;
                // Run short warmup and A/B benchmark on the real FFN workload
                AdaBenchmarkResult bench = BenchmarkAdaW4A8Ffn(adaParams, 10.0f);
                if (bench.valid) {
                    s.lockedBackend = bench.winner;
                    s.sessionLocked = true;
                    s.lastWasCached = false;
                    s.lastDp4aUs = bench.dp4aMedianUs;
                    s.lastTcUs = bench.tensorCoreMedianUs;
                    WriteCache(s.cacheFilePath, cacheKey, s.lockedBackend, bench.dp4aMedianUs, bench.tensorCoreMedianUs);
                    const char* choiceStr = (s.lockedBackend == AdaW4A8ComputeMode::TensorCore) ? "TensorCore" : "DP4A";
                    std::fprintf(stderr, "\nNRFusion Ada Auto\nDP4A:       %.1f us\nTensorCore: %.1f us\nSelected: %s\n\n",
                                 bench.dp4aMedianUs, bench.tensorCoreMedianUs, choiceStr);
                    std::fflush(stderr);
                } else {
                    s.lockedBackend = AdaW4A8ComputeMode::Dp4a;
                    s.sessionLocked = true;
                }
            }
        }
    }

    if (!ArmedForAdaW4A8(s)) {
        RescueSuppressedPublish(s, cmdList);
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.fallbackLaunches;
        return false;
    }

    const auto dispatch = [&](const AdaW4A8Params& p) noexcept -> AdaW4A8Status {
        if (g_cubinLauncher != nullptr)
            return g_cubinLauncher(cmdList, p) ? AdaW4A8Status::Ok : AdaW4A8Status::LaunchFailed;
        return LaunchAdaW4A8Ffn(p);
    };

    // Launch via locked backend
    adaParams.computeMode = s.lockedBackend;
    auto status = dispatch(adaParams);

    // Rule 12: TensorCore failed -> Fallback to DP4A
    if (status != AdaW4A8Status::Ok && s.lockedBackend == AdaW4A8ComputeMode::TensorCore) {
        adaParams.computeMode = AdaW4A8ComputeMode::Dp4a;
        status = dispatch(adaParams);
        if (status == AdaW4A8Status::Ok) {
            std::lock_guard<std::recursive_mutex> lock(s.mutex);
            s.lockedBackend = AdaW4A8ComputeMode::Dp4a;
            s.status = "Ada W4A8 active (fallback to DP4A after TensorCore failure)";
        }
    }

    if (status == AdaW4A8Status::Ok) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ++s.interceptedLaunches;
        if (blockSlot < static_cast<int>(s.blockReplaced.size())) s.blockReplaced[blockSlot] = true;
        s.suppressedExpandPublish.erase(cmdList);
        const char* modeLabel = (s.lockedBackend == AdaW4A8ComputeMode::Dp4a) ? "CUDA Core dp4a" : "INT8 Tensor Core";
        if (s.configMode == AdaW4A8ComputeMode::Auto) {
            char statBuf[256]{};
            if (s.lastDp4aUs > 0.0f && s.lastTcUs > 0.0f) {
                std::snprintf(statBuf, sizeof(statBuf), "Ada W4A8 active (Auto -> %s; DP4A=%.1fus, TC=%.1fus%s): %llu launches",
                              modeLabel, s.lastDp4aUs, s.lastTcUs, s.lastWasCached ? " [cached]" : "", (unsigned long long)s.interceptedLaunches);
            } else {
                std::snprintf(statBuf, sizeof(statBuf), "Ada W4A8 active (Auto -> %s%s): %llu launches",
                              modeLabel, s.lastWasCached ? " [cached]" : "", (unsigned long long)s.interceptedLaunches);
            }
            s.status = statBuf;
        } else {
            s.status = std::string("Ada W4A8 active (") + modeLabel + " [manual]): " +
                       std::to_string(s.interceptedLaunches) + " launches";
        }
        return true;
    }

    // Clean fallback to original dispatch if launch fails. If this block's expand was suppressed a
    // moment ago, the word it would have published still has to be written, or the stage that now
    // runs for real waits on it and the device hangs.
    RescueSuppressedPublish(s, cmdList);

    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    ++s.fallbackLaunches;
    s.status = "Ada W4A8 fallback: launch returned " + std::string(Describe(status));
    return false;
}

void NoteAdaW4A8Kernel(void* function, const char* name) noexcept {
    if (!function || !name) return;
    auto& s = State();

    // The feed-forward of the 512-channel blocks. The projecting stage has to be tested first,
    // because its name contains the expanding stage's name.
    int stage = -1;
    if (std::strstr(name, "cc_split_swin_16h_ffwd_proj") != nullptr) stage = kProjectStage;
    else if (std::strstr(name, "cc_split_swin_16h_ffwd") != nullptr) stage = kExpandStage;
    // Another family entirely: the bottleneck runs a plain 1024 -> 4096 -> 4096 -> 1024 feed-forward
    // through these, and its argument block is the same size as the projecting stage's. Naming it
    // here and refusing it at the launch is what stops one family's weights reaching the other's
    // kernel, which is what the path used to do.
    else if (std::strstr(name, "cc_vit_1d_ffn") != nullptr ||
             std::strstr(name, "cc_vit_ffn") != nullptr) stage = kForeignStage;
    if (stage >= 0) {
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        s.ffnKernels[function] = stage;
        return;
    }

    // Each resolution level ships two dozen variants of its layer -- chained, waiting, tile
    // synchronised, downsampling, upsampling. They differ in which fields of the block they read,
    // never in where those fields sit, so the family prefix is what decides the layout.
    static const char kSwinPrefix[] = "cc_tinlayout_fused_swin_";
    if (std::strncmp(name, kSwinPrefix, sizeof(kSwinPrefix) - 1) != 0) return;
    const char* suffix = name + sizeof(kSwinPrefix) - 1;
    for (int index = 0; index < kSwinFamilies; ++index) {
        const char* family = kSwinAbi[index].family;
        const size_t length = std::strlen(family);
        if (std::strncmp(suffix, family, length) != 0) continue;
        if (suffix[length] != '\0' && suffix[length] != '_') continue;
        std::lock_guard<std::recursive_mutex> lock(s.mutex);
        s.swinKernels[function] = index;
        return;
    }
}

void SetAdaW4A8CubinLauncher(AdaW4A8CubinLauncher launcher) noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    g_cubinLauncher = launcher;
}

bool HasAdaW4A8CubinLauncher() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return g_cubinLauncher != nullptr;
}

bool IsAdaW4A8Accelerating() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return s.interceptedLaunches > 0;
}

std::uint64_t AdaW4A8ReplacedLaunches() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return s.interceptedLaunches;
}

const char* GetAdaW4A8Status() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    // The old buffer was 160 bytes for a line that already needed more, so the counters at the end
    // were cut off exactly when they were being read.
    char counters[512]{};
    std::snprintf(counters, sizeof(counters),
                  " | seen=%llu paramSize=%u expand=%llu project=%llu bound=%u/%u unbound=%llu"
                  " blockMismatch=%llu expandSuppressed=%llu rescued=%llu paired=%d windowsOk=%llu intercepted=%llu fallback=%llu rejectedWindows=%llu dims=%ux%u",
                  (unsigned long long)s.observedLaunches, s.lastParamSize,
                  (unsigned long long)s.ffwdExpandLaunches, (unsigned long long)s.ffwdProjectLaunches,
                  (unsigned)s.boundBlocks, (unsigned)s.blocks.size(),
                  (unsigned long long)s.ffwdUnbound, (unsigned long long)s.ffwdBlockMismatches,
                  (unsigned long long)s.ffwdExpandSuppressed,
                  (unsigned long long)s.ffwdRescuedPublishes, s.pairingProven ? 1 : 0,
                  (unsigned long long)s.ffnWindowsAccepted,
                  (unsigned long long)s.interceptedLaunches, (unsigned long long)s.fallbackLaunches,
                  (unsigned long long)s.lastRejectedWindows, s.lastDims[0], s.lastDims[1]);
    s.statusScratch = s.status + counters;

    for (int index = 0; index < kSwinFamilies; ++index) {
        if (s.swinLaunches[index] == 0) continue;
        char swin[96]{};
        std::snprintf(swin, sizeof(swin), " swin_%s=%llu(%ux%u)", kSwinAbi[index].family,
                      (unsigned long long)s.swinLaunches[index],
                      s.swinDims[index][0], s.swinDims[index][1]);
        s.statusScratch += swin;
    }
    if (s.swinBlockMismatches) {
        char swin[64]{};
        std::snprintf(swin, sizeof(swin), " swinBlockMismatch=%llu",
                      (unsigned long long)s.swinBlockMismatches);
        s.statusScratch += swin;
    }
    return s.statusScratch.c_str();
}

void SetAdaW4A8Enabled(bool enabled) noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.enabled = enabled;
    if (!enabled) {
        s.status = "Ada W4A8: bypassed (standard NVIDIA FP8 active)";
    } else {
        if (s.sessionLocked) {
            const char* modeLabel = (s.lockedBackend == AdaW4A8ComputeMode::Dp4a) ? "CUDA Core dp4a" : "INT8 Tensor Core";
            s.status = std::string("Ada W4A8 active (") + modeLabel + ")";
        } else {
            s.status = "Ada W4A8 active (Auto)";
        }
    }
}

bool IsAdaW4A8Enabled() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    return s.enabled;
}

void SetAdaW4A8Backend(const char* backendName) noexcept {
    if (!backendName) return;
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    std::string m(backendName);
    for (char& c : m) c = static_cast<char>(std::tolower(c));
    if (m.find("tensor") != std::string::npos || m == "tc" || m == "wmma") {
        s.configMode = AdaW4A8ComputeMode::TensorCore;
        s.lockedBackend = AdaW4A8ComputeMode::TensorCore;
        s.sessionLocked = true;
        s.status = ReadyStatus("INT8 Tensor Core [manual override]");
    } else if (m.find("dp4a") != std::string::npos || m.find("cuda") != std::string::npos || m == "alu") {
        s.configMode = AdaW4A8ComputeMode::Dp4a;
        s.lockedBackend = AdaW4A8ComputeMode::Dp4a;
        s.sessionLocked = true;
        s.status = ReadyStatus("CUDA Core dp4a [manual override]");
    } else {
        s.configMode = AdaW4A8ComputeMode::Auto;
        s.sessionLocked = false;
        s.status = "Ada W4A8 active (Auto): evaluating optimal backend";
    }
}

const char* GetAdaW4A8Backend() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.configMode == AdaW4A8ComputeMode::TensorCore) return "TensorCore";
    if (s.configMode == AdaW4A8ComputeMode::Dp4a) return "DP4A";
    return "Auto";
}

void RecalibrateAdaW4A8Backend() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.forceBenchmark = true;
    s.sessionLocked = false;
    s.status = "Ada W4A8: recalibrating fastest backend on next launch...";
}

bool IsBlackwellArchitecture() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.initialized) {
        IsAdaSm89Architecture(nullptr);
    }
    return s.isBlackwell;
}

const char* GetGpuArchitectureName() noexcept {
    auto& s = State();
    std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.initialized) {
        IsAdaSm89Architecture(nullptr);
    }
    return s.archName.c_str();
}

} // namespace nrfusion
