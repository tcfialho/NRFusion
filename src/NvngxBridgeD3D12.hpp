#pragma once

#include <d3d12.h>
#include <cstdint>

#ifdef NRFUSION_NVNGX_BRIDGE_EXPORTS
#define NRFUSION_BRIDGE_API __declspec(dllexport)
#else
#define NRFUSION_BRIDGE_API __declspec(dllimport)
#endif

extern "C" {

NRFUSION_BRIDGE_API void dlssnr_call_set_float_slot(int slot);

NRFUSION_BRIDGE_API void dlssnr_call_probe_float(
    void* params, const char* name, float value, int slot);

NRFUSION_BRIDGE_API extern int dlssnr_call_last_init;
NRFUSION_BRIDGE_API extern int dlssnr_call_last_create;
NRFUSION_BRIDGE_API const char* dlssnr_call_error();

NRFUSION_BRIDGE_API void* dlssnr_call_create(
    const wchar_t* snippetPath,
    const wchar_t* dataPath,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmd,
    void* capabilityParams,
    unsigned int width,
    unsigned int height,
    int preset,
    float intensity,
    int style,
    float localStructure,
    float localTone,
    float skinStructure,
    int useAutoMask,
    int uiCorrection);

NRFUSION_BRIDGE_API int dlssnr_call_evaluate_v2(
    ID3D12GraphicsCommandList* cmd,
    void* feature,
    void* capabilityParams,
    ID3D12Resource* color,
    ID3D12Resource* depth,
    ID3D12Resource* motion,
    ID3D12Resource* output,
    unsigned int width,
    unsigned int height,
    unsigned int guideWidth,
    unsigned int guideHeight,
    unsigned int motionWidth,
    unsigned int motionHeight,
    unsigned int depthBaseX,
    unsigned int depthBaseY,
    unsigned int motionBaseX,
    unsigned int motionBaseY,
    int depthInverted,
    int reset,
    float intensity,
    int style,
    float localStructure,
    float localTone,
    float skinStructure,
    int useAutoMask,
    float motionScaleX,
    float motionScaleY);

NRFUSION_BRIDGE_API void dlssnr_call_set_extras(
    void* capabilityParams,
    float globalTone,
    ID3D12Resource* ui,
    ID3D12Resource* uiAlpha,
    ID3D12Resource* backbuffer,
    unsigned int uiWidth,
    unsigned int uiHeight,
    unsigned int backbufferWidth,
    unsigned int backbufferHeight);

NRFUSION_BRIDGE_API void dlssnr_call_release(void* feature);

} // extern "C"
