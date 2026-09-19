#pragma once

#include <cstdint>

namespace nrfusion {

struct CaptureD3D11TransportInfo {
    bool configured = false;
    uint32_t nativeWidth = 0;
    uint32_t nativeHeight = 0;
    uint32_t workWidth = 0;
    uint32_t workHeight = 0;
    uint32_t nativeFormat = 0;
    uint32_t transportFormat = 0;
    uint64_t downsampleDispatches = 0;
    uint32_t processingMode = 0;
    uint64_t composeDispatches = 0;
    bool hasDepthGuide = false;
    uint64_t depthCaptureDispatches = 0;
};

// Starts the minimal D3D11 capture hook from the capture DLL. It owns no NR objects:
// its only responsibility is Present-side resource acquisition and IPC transport setup.
void StartCaptureD3D11Hooks();
void StopCaptureD3D11Hooks();
void SetCaptureD3D11WorkingScale(float workingScale);
void SetCaptureD3D11ProcessingMode(uint32_t mode);
bool GetCaptureD3D11TransportInfo(CaptureD3D11TransportInfo& outInfo);

} // namespace nrfusion
