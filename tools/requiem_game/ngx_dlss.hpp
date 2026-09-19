#pragma once

// A minimal DLSS call, loaded at run time.
//
// The testbed needs to look like a DLSS game to whatever is intercepting, because that is what
// makes the neural pass run and its kernels launch. It does not need the NGX import library to
// do that: the entry points are exported by the proxy sitting beside the executable, or by the
// driver's own loader, and both are resolved by name at run time.
//
// The caller must stop a measurement if loading, creation or evaluation fails.
// Creation alone does not establish that the host executed NR.

#include <d3d11.h>
#include <d3d12.h>
#include <windows.h>

#include <cstdint>
#include <string>

namespace requiem {

// Parameter names as the runtime spells them. Taken from the vendor's own defines, not guessed.
constexpr const char* kParamWidth = "Width";
constexpr const char* kParamHeight = "Height";
constexpr const char* kParamOutWidth = "OutWidth";
constexpr const char* kParamOutHeight = "OutHeight";
constexpr const char* kParamPerfQuality = "PerfQualityValue";
constexpr const char* kParamCreationNodeMask = "CreationNodeMask";
constexpr const char* kParamVisibilityNodeMask = "VisibilityNodeMask";
constexpr const char* kParamCreateFlags = "DLSS.Feature.Create.Flags";
constexpr const char* kParamColor = "Color";
constexpr const char* kParamOutput = "Output";
constexpr const char* kParamDepth = "Depth";
constexpr const char* kParamMotion = "MotionVectors";
constexpr const char* kParamJitterX = "Jitter.Offset.X";
constexpr const char* kParamJitterY = "Jitter.Offset.Y";
constexpr const char* kParamMvScaleX = "MV.Scale.X";
constexpr const char* kParamMvScaleY = "MV.Scale.Y";
constexpr const char* kParamReset = "Reset";
constexpr const char* kParamSubrectWidth = "DLSS.Render.Subrect.Dimensions.Width";
constexpr const char* kParamSubrectHeight = "DLSS.Render.Subrect.Dimensions.Height";

// The parameter object is an interface with virtual setters, not a handle with exported
// functions, so calling it means matching the vendor's declaration order exactly. This mirrors
// it; getting the order wrong would call the wrong setter with no diagnostic at all.
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

struct NgxHandle;

class Dlss {
public:
    bool Load();                       // resolve the entry points; false when nothing answers
    bool Init(ID3D12Device* device, const wchar_t* applicationPath);
    bool Create(ID3D12GraphicsCommandList* commands, std::uint32_t renderWidth,
                std::uint32_t renderHeight, std::uint32_t outputWidth, std::uint32_t outputHeight);
    bool Evaluate(ID3D12GraphicsCommandList* commands, ID3D12Resource* colour,
                  ID3D12Resource* output, ID3D12Resource* depth, ID3D12Resource* motion,
                  float jitterX, float jitterY, bool reset);
    void Shutdown();

    bool Ready() const noexcept { return handle_ != nullptr; }
    const std::string& Status() const noexcept { return status_; }
    const std::string& Library() const noexcept { return library_; }

private:
    using InitFn = int(__cdecl*)(unsigned long long, const wchar_t*, ID3D12Device*, const void*, unsigned int);
    using AllocFn = int(__cdecl*)(NgxParameter**);
    using CapFn = int(__cdecl*)(NgxParameter**);
    using CreateFn = int(__cdecl*)(ID3D12GraphicsCommandList*, int, NgxParameter*, NgxHandle**);
    using EvalFn = int(__cdecl*)(ID3D12GraphicsCommandList*, const NgxHandle*, NgxParameter*, void*);
    using ReleaseFn = int(__cdecl*)(NgxHandle*);
    using ShutdownFn = int(__cdecl*)();

    HMODULE module_ = nullptr;
    InitFn init_ = nullptr;
    AllocFn allocate_ = nullptr;
    CapFn capabilities_ = nullptr;
    CreateFn createFeature_ = nullptr;
    EvalFn evaluate_ = nullptr;
    ReleaseFn release_ = nullptr;
    ShutdownFn shutdown_ = nullptr;

    NgxParameter* parameters_ = nullptr;
    NgxHandle* handle_ = nullptr;
    std::uint32_t renderWidth_ = 0, renderHeight_ = 0;
    std::string status_ = "nao carregado";
    std::string library_;
};

} // namespace requiem
