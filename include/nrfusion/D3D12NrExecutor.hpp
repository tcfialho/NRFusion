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

class D3D12NrExecutor {
public:
    bool Load();

    bool Init(ID3D12Device* device);

    bool EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                       const DlssNrTuning& tuning = {});
    bool JustBuilt() const noexcept { return justBuilt_; }

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
