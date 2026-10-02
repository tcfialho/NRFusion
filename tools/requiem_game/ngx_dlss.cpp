#include "ngx_dlss.hpp"

#include <array>
#include <filesystem>

namespace requiem {
namespace {

// The proxy beside the executable comes first on purpose. It is the one that routes the frame
// through the path being measured; the driver's own loader would run stock DLSS and the neural
// pass under test would never execute.
constexpr std::array<const wchar_t*, 6> kCandidates = {
    L"dxgi.dll", L"winmm.dll", L"version.dll", L"nvngx.dll", L"_nvngx.dll", L"nvngx_dlss.dll"};

constexpr int kFeatureSuperSampling = 1;   // NVSDK_NGX_Feature_SuperSampling
constexpr int kSuccess = 1;                // NVSDK_NGX_Result_Success

template <typename T>
T Symbol(HMODULE module, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

HMODULE LoadCandidate(const wchar_t* name) {
    wchar_t exe[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        const auto local = std::filesystem::path(exe).parent_path() / name;
        if (std::filesystem::is_regular_file(local)) {
            if (HMODULE module = LoadLibraryW(local.c_str())) return module;
        }
    }
    if (HMODULE module = GetModuleHandleW(name)) return module;
    return LoadLibraryW(name);
}

} // namespace

bool Dlss::Load() {
    for (const wchar_t* candidate : kCandidates) {
        HMODULE module = LoadCandidate(candidate);
        if (!module) continue;
        const bool isNRFusion =
            GetProcAddress(module, "NRFusion_Capture32_Connect") != nullptr;
        nrfusionProxySeen_ = nrfusionProxySeen_ || isNRFusion;
        if (!Symbol<CreateFn>(module, "NVSDK_NGX_D3D12_CreateFeature")) continue;
        module_ = module;
        nrfusionProxy_ = isNRFusion;
        char path[MAX_PATH]{};
        GetModuleFileNameA(module_, path, MAX_PATH);
        library_ = path;
        break;
    }
    if (!module_) {
        status_ = nrfusionProxySeen_
            ? "proxy NRFusion carregado sem exports NGX D3D12"
            : "nenhuma biblioteca com os pontos de entrada NGX D3D12";
        return false;
    }

    init_ = Symbol<InitFn>(module_, "NVSDK_NGX_D3D12_Init");
    allocate_ = Symbol<AllocFn>(module_, "NVSDK_NGX_D3D12_AllocateParameters");
    capabilities_ = Symbol<CapFn>(module_, "NVSDK_NGX_D3D12_GetCapabilityParameters");
    createFeature_ = Symbol<CreateFn>(module_, "NVSDK_NGX_D3D12_CreateFeature");
    evaluate_ = Symbol<EvalFn>(module_, "NVSDK_NGX_D3D12_EvaluateFeature");
    release_ = Symbol<ReleaseFn>(module_, "NVSDK_NGX_D3D12_ReleaseFeature");
    shutdown_ = Symbol<ShutdownFn>(module_, "NVSDK_NGX_D3D12_Shutdown");

    if (!init_ || !createFeature_ || !evaluate_ || (!allocate_ && !capabilities_)) {
        status_ = "biblioteca NGX incompleta para D3D12";
        module_ = nullptr;
        return false;
    }
    status_ = "carregada";
    return true;
}

bool Dlss::Init(ID3D12Device* device, const wchar_t* applicationPath) {
    if (!module_) return false;
    // Use NVIDIA's public Streamline Sample id so NGX and Streamline share a valid
    // test application identity without borrowing an id from a shipping title.
    if (init_(231313132ull, applicationPath, device, nullptr, 0x15u) != kSuccess) {
        status_ = "NVSDK_NGX_D3D12_Init recusou";
        return false;
    }
    if (allocate_ && allocate_(&parameters_) != kSuccess) parameters_ = nullptr;
    if (!parameters_ && capabilities_ && capabilities_(&parameters_) != kSuccess) parameters_ = nullptr;
    if (!parameters_) {
        status_ = "nao consegui alocar parametros NGX";
        return false;
    }
    status_ = "inicializada";
    return true;
}

bool Dlss::Create(ID3D12GraphicsCommandList* commands, std::uint32_t renderWidth,
                  std::uint32_t renderHeight, std::uint32_t outputWidth,
                  std::uint32_t outputHeight, int quality) {
    if (!parameters_) return false;
    renderWidth_ = renderWidth;
    renderHeight_ = renderHeight;
    parameters_->Set(kParamWidth, renderWidth);
    parameters_->Set(kParamHeight, renderHeight);
    parameters_->Set(kParamOutWidth, outputWidth);
    parameters_->Set(kParamOutHeight, outputHeight);
    parameters_->Set(kParamPerfQuality, quality);
    parameters_->Set(kParamCreationNodeMask, 1u);
    parameters_->Set(kParamVisibilityNodeMask, 1u);
    // Motion vectors are produced at render resolution and the depth is the usual inverted
    // range, which is what the shader in this testbed writes.
    parameters_->Set(kParamCreateFlags, (1 << 1) | (1 << 3));

    if (createFeature_(commands, kFeatureSuperSampling, parameters_, &handle_) != kSuccess ||
        !handle_) {
        handle_ = nullptr;
        status_ = "CreateFeature recusou";
        return false;
    }
    status_ = "feature criada; avaliacao ainda nao executada";
    return true;
}

bool Dlss::Evaluate(ID3D12GraphicsCommandList* commands, ID3D12Resource* colour,
                    ID3D12Resource* output, ID3D12Resource* depth, ID3D12Resource* motion,
                    float jitterX, float jitterY, bool reset) {
    if (!handle_ || !parameters_) return false;
    parameters_->Set(kParamColor, colour);
    parameters_->Set(kParamOutput, output);
    parameters_->Set(kParamDepth, depth);
    parameters_->Set(kParamMotion, motion);
    parameters_->Set(kParamJitterX, jitterX);
    parameters_->Set(kParamJitterY, jitterY);
    // Vectors are in normalised device units here, so the runtime is told to scale them to
    // pixels itself rather than being handed a quantity in the wrong space.
    parameters_->Set(kParamMvScaleX, static_cast<float>(renderWidth_));
    parameters_->Set(kParamMvScaleY, static_cast<float>(renderHeight_));
    parameters_->Set(kParamReset, reset ? 1 : 0);
    parameters_->Set(kParamSubrectWidth, renderWidth_);
    parameters_->Set(kParamSubrectHeight, renderHeight_);
    const int result = evaluate_(commands, handle_, parameters_, nullptr);
    status_ = result == kSuccess ? "avaliacao submetida (nao comprova NR)"
                                 : "EvaluateFeature falhou: " + std::to_string(result);
    return result == kSuccess;
}

void Dlss::ReleaseFeature() {
    if (handle_ && release_) release_(handle_);
    handle_ = nullptr;
}

void Dlss::Shutdown() {
    ReleaseFeature();
    if (shutdown_) shutdown_();
}

} // namespace requiem
