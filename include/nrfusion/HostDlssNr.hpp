#pragma once

// Standalone DLSS Neural Rendering caller for NRFusionHost64.exe.
//
// OptiScaler.dll cannot run this itself for an x86 game: the model is 64-bit only, so a 32-bit
// game process forwards its frame over IPC (CaptureProvider32/HostServer64) to this 64-bit host,
// which must invoke DLSS-NR (NGX feature 18) on its own -- there is no OptiScaler Config/State to
// borrow values from here, so every tuning knob below is an explicit parameter with the same
// default OptiScaler.ini ships (see Config.h: DlssNrIntensity/Style/LocalStructure/LocalTone/
// SkinStructure/AutoMask/Preset), not a guess.
//
// The call sequence and every parameter name are taken from the already-shipped, already-working
// nvngx.dll_dlssnr.dll forwarder (OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp) and its
// dlssnr_call_create/dlssnr_call_evaluate_v2/dlssnr_call_release exports, the same binary the
// D3D12/D3D11/Vulkan Synthetic routes already load. This class does not reimplement or bypass any
// driver check; it is a second, independent caller of the same already-proven mechanism.
//
// The parameter interface below mirrors the layout tools/requiem_game/ngx_dlss.hpp already uses
// for the same reason that file gives: the vendor's parameter object is a vtable, not an export
// table, and its method order must match exactly. This is an independent declaration of that
// order, not a copy of NVIDIA's SDK header.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

#include <cstdint>
#include <string>

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

// Every field defaults to OptiScaler.ini's own default (Config.h), so a caller that sets nothing
// gets the same tuning a fresh OptiScaler install would use.
struct DlssNrTuning {
    int preset = 0;
    float intensity = 1.0f;
    int style = 0;
    float localStructure = 1.0f;
    float localTone = 1.0f;
    float skinStructure = -1.0f;
    bool autoMask = true;
    int uiCorrection = 1;
};

class HostDlssNr {
public:
    // Resolves nvngx.dll/_nvngx.dll (the driver's standard NGX entry points, for capability
    // parameters) and nvngx.dll_dlssnr.dll (the caller-gate forwarder) beside this executable.
    bool Load();

    bool Init(ID3D12Device* device);

    // Builds the feature once per (device, resolution). Cheap to call again: a no-op if the
    // existing feature already matches width/height.
    //
    // Creation records work into cmdList; evaluating that same feature before this list has been
    // submitted is a GPU hang (OptiScaler.dll's own DlssNr_Dx12.cpp documents hitting this and
    // removing the multi-pass path that caused it). A caller must check JustBuilt() and, when
    // true, submit this list and skip Evaluate() for this frame -- the feature is usable starting
    // next call.
    bool EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                       const DlssNrTuning& tuning = {});
    bool JustBuilt() const noexcept { return justBuilt_; }

    // Color/output are always the feature's own working resolution (width, height). Depth and
    // motion carry their own size: a real game's depth/motion may be native-resolution even when
    // color is downsampled, which is exactly what the forwarder's own subrect parameters are for
    // (dlssnr_forwarder.cpp: "depth and motion come from the game's own DLSS evaluation and may
    // be render resolution"). Defaulting guideWidth/guideHeight/motionWidth/motionHeight to 0
    // means "same as width/height" -- the zero-guide placeholder case.
    bool Evaluate(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color, ID3D12Resource* depth,
                 ID3D12Resource* motion, ID3D12Resource* output, uint32_t width, uint32_t height,
                 bool depthInverted, bool reset, const DlssNrTuning& tuning = {},
                 uint32_t guideWidth = 0, uint32_t guideHeight = 0, uint32_t motionWidth = 0,
                 uint32_t motionHeight = 0);

    void Shutdown();

    bool Ready() const noexcept { return feature_ != nullptr; }
    bool IsInitialised() const noexcept { return capabilityParams_ != nullptr; }
    const std::string& Status() const noexcept { return status_; }

private:
    // Same argument order tools/requiem_game/ngx_dlss.cpp already calls successfully on this
    // machine's driver: appId, application path, device, feature-common info (unused), sdk version.
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

    void DiscoverFloatSlot();

    HMODULE driverModule_ = nullptr;
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
    bool floatSlotKnown_ = false;
    bool justBuilt_ = false;
    std::wstring snippetPath_;
    std::string status_ = "not loaded";
};

} // namespace nrfusion
