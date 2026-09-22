#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

#include <array>
#include <cstdint>
#include <string>

#include "nrfusion/D3D12NrCodec.hpp"
#include "nrfusion/D3D12NrFramePlan.hpp"
#include "nrfusion/D3D12NrGuideClones.hpp"
#include "nrfusion/D3D12NrScratchResources.hpp"
#include "nrfusion/NrDeferredRetirementQueue.hpp"
#include "nrfusion/NrSubmissionGate.hpp"

namespace nrfusion {

struct NgxParameter {
    virtual void Set(const char* name, unsigned long long value) = 0;
    virtual void Set(const char* name, float value) = 0;
    virtual void Set(const char* name, double value) = 0;
    virtual void Set(const char* name, unsigned int value) = 0;
    virtual void Set(const char* name, int value) = 0;
    virtual void Set(const char* name, ID3D11Resource* value) = 0;
    virtual void Set(const char* name, ID3D12Resource* value) = 0;
    virtual void Set(const char* name, void* value) = 0;
    virtual int Get(const char* name, unsigned long long* out) const = 0;
    virtual int Get(const char* name, float* out) const = 0;
    virtual int Get(const char* name, double* out) const = 0;
    virtual int Get(const char* name, unsigned int* out) const = 0;
    virtual int Get(const char* name, int* out) const = 0;
    virtual int Get(const char* name, ID3D11Resource** out) const = 0;
    virtual int Get(const char* name, ID3D12Resource** out) const = 0;
    virtual int Get(const char* name, void** out) const = 0;
    virtual void Reset() = 0;
};

struct DlssNrTuning {
    int preset = 0;
    float intensity = 1.0f;
    int style = 0;
    float localStructure = 1.0f;
    float localTone = 1.0f;
    float skinStructure = -1.0f;
    bool autoMask = true;
    int uiCorrection = 1;

    bool operator==(const DlssNrTuning&) const noexcept = default;
};


constexpr std::uint32_t kD3D12NrMaxPassCount = 30;

struct D3D12NrComposition {
    bool runBeforeUpscale = false;
    bool rayReconstruction = false;
    bool residualAcrossRr = false;
    bool colourIsLinearHdr = true;
    bool useGameExposure = false;
    float whitePoint = 1.0f;
    float exposurePreMul = 1.0f;
    float transferStrength = 1.0f;
    float colourStrength = 1.0f;
    float maxRatio = 4.0f;
    std::uint32_t debugView = 0;
    std::uint32_t transfer = 0;
    std::uint32_t reversibleMode = 0;
    bool applyModel = true;
    std::uint32_t skinProtection = 0;
    std::uint32_t showSkinMask = 0;
    float skinDetail = 1.0f;
    float skinColour = 1.0f;
    float environmentDetail = 1.0f;
    float environmentColour = 1.0f;
    float residualBlend = 1.0f;
};

struct D3D12NrFrameResources {
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* output = nullptr;
    ID3D12Resource* exposure = nullptr;
};

struct D3D12NrFrameRequest {
    D3D12NrFramePlanInput plan{};
    std::array<DlssNrTuning, kD3D12NrMaxPassCount> tuning{};
    D3D12NrComposition composition{};
    std::uint64_t submissionEpoch = 0;
    bool reset = false;
    bool depthInverted = false;
    float motionScaleX = 1.0f;
    float motionScaleY = 1.0f;
    D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES outputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES motionState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
};

enum class D3D12NrFrameResult : std::uint8_t {
    Applied,
    SkippedPlacement,
    PendingFeature,
    Failed
};

class D3D12NrExecutor {
public:
    D3D12NrExecutor() = default;
    D3D12NrExecutor(const D3D12NrExecutor&) = delete;
    D3D12NrExecutor& operator=(const D3D12NrExecutor&) = delete;
    D3D12NrExecutor(D3D12NrExecutor&&) = delete;
    D3D12NrExecutor& operator=(D3D12NrExecutor&&) = delete;

    bool Load();

    bool Init(ID3D12Device* device);

    bool EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                       const DlssNrTuning& tuning = {});
    bool EnsureFeatureForEpoch(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                               std::uint64_t submissionEpoch, const DlssNrTuning& tuning = {});
    bool JustBuilt() const noexcept { return justBuilt_; }
    bool PendingSubmission() const noexcept { return submissionGate_.Pending(); }
    std::uint64_t FeatureCreateEpoch() const noexcept { return submissionGate_.CreateEpoch(); }

    bool Evaluate(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color, ID3D12Resource* depth,
                  ID3D12Resource* motion, ID3D12Resource* output, uint32_t width, uint32_t height,
                  bool depthInverted, bool reset, const DlssNrTuning& tuning = {},
                  uint32_t guideWidth = 0, uint32_t guideHeight = 0, uint32_t motionWidth = 0,
                  uint32_t motionHeight = 0, float motionScaleX = 1.0f,
                  float motionScaleY = 1.0f);
    bool EvaluateForEpoch(
        ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color, ID3D12Resource* depth,
        ID3D12Resource* motion, ID3D12Resource* output, uint32_t width, uint32_t height,
        std::uint64_t submissionEpoch, bool depthInverted, bool reset,
        const DlssNrTuning& tuning = {}, uint32_t guideWidth = 0, uint32_t guideHeight = 0,
        uint32_t motionWidth = 0, uint32_t motionHeight = 0,
        float motionScaleX = 1.0f, float motionScaleY = 1.0f);

    D3D12NrFrameResult ExecuteFrame(
        ID3D12GraphicsCommandList* cmdList,
        const D3D12NrFrameResources& resources,
        const D3D12NrFrameRequest& request);

    void Shutdown();

    bool Ready() const noexcept { return feature_ != nullptr; }
    bool IsInitialised() const noexcept { return capabilityParams_ != nullptr; }
    const std::string& Status() const noexcept { return status_; }

private:
    static constexpr int kNgxSuccess = 1;

    using InitFn = int(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*, const void*, unsigned int);
    using GetCapFn = int(__cdecl*)(NgxParameter**);
    using CreateFn = void*(__cdecl*)(const wchar_t*, const wchar_t*, ID3D12Device*,
                                     ID3D12GraphicsCommandList*, void*, unsigned int, unsigned int,
                                     int, float, int, float, float, float, int, int);
    using EvaluateFn = int(__cdecl*)(ID3D12GraphicsCommandList*, void*, void*, ID3D12Resource*,
                                     ID3D12Resource*, ID3D12Resource*, ID3D12Resource*, unsigned int,
                                     unsigned int, unsigned int, unsigned int, unsigned int, unsigned int,
                                     unsigned int, unsigned int, unsigned int, unsigned int, int, int,
                                     float, int, float, float, float, int, float, float);
    using ReleaseFn = void(__cdecl*)(void*);
    using SetFloatSlotFn = void(__cdecl*)(int);
    using ProbeFloatFn = void(__cdecl*)(void*, const char*, float, int);

    static void ReleaseRetired(void* context, NrRetiredObject retired) noexcept;
    void DiscoverFloatSlot();
    bool RetirePassFeatures(std::uint32_t first) noexcept;
    std::uint32_t PreparePassFeatures(
        ID3D12GraphicsCommandList* cmdList, std::uint32_t width, std::uint32_t height,
        std::uint32_t requested, std::uint64_t epoch,
        const std::array<DlssNrTuning, kD3D12NrMaxPassCount>& tuning,
        bool& pending) noexcept;
    bool EvaluateFeature(
        void* feature, ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* color, ID3D12Resource* depth, ID3D12Resource* motion,
        ID3D12Resource* output, std::uint32_t width, std::uint32_t height,
        std::uint32_t guideWidth, std::uint32_t guideHeight,
        std::uint32_t motionWidth, std::uint32_t motionHeight,
        std::uint32_t depthBaseX, std::uint32_t depthBaseY,
        std::uint32_t motionBaseX, std::uint32_t motionBaseY,
        bool depthInverted, bool reset, const DlssNrTuning& tuning,
        float motionScaleX, float motionScaleY) noexcept;

    HMODULE driverModule_ = nullptr;
    bool driverModuleOwned_ = false;
    HMODULE forwarderModule_ = nullptr;
    InitFn driverInit_ = nullptr;
    GetCapFn getCapabilityParams_ = nullptr;
    CreateFn create_ = nullptr;
    EvaluateFn evaluate_ = nullptr;
    ReleaseFn release_ = nullptr;
    SetFloatSlotFn setFloatSlot_ = nullptr;
    ProbeFloatFn probeFloat_ = nullptr;

    NgxParameter* capabilityParams_ = nullptr;
    void* feature_ = nullptr;
    uint32_t featureWidth_ = 0;
    uint32_t featureHeight_ = 0;
    DlssNrTuning featureTuning_{};
    bool featureTuningValid_ = false;
    bool floatSlotKnown_ = false;
    bool justBuilt_ = false;
    NrSubmissionGate submissionGate_{};
    NrDeferredRetirementQueue retirement_{};
    D3D12NrScratchResources scratch_{};
    D3D12NrGuideClones guideClones_{};
    D3D12NrCodec codec_{};
    std::array<void*, kD3D12NrMaxPassCount> passFeatures_{};
    std::array<NrSubmissionGate, kD3D12NrMaxPassCount> passGates_{};
    std::array<DlssNrTuning, kD3D12NrMaxPassCount> passTunings_{};
    std::array<bool, kD3D12NrMaxPassCount> passTuningValid_{};
    std::array<bool, kD3D12NrMaxPassCount> passNeedsReset_{};
    std::array<bool, kD3D12NrMaxPassCount> passCreateFailed_{};
    std::uint32_t residualHistoryIndex_ = 0;
    bool residualHistoryPrimed_ = false;
    bool residualStoreValid_ = false;
    std::uint64_t residualEpoch_ = 0;
    std::wstring snippetPath_;
    std::string status_ = "not loaded";
};

} // namespace nrfusion
