#ifndef NRFUSION_CAPTURE32_EXPORTS
#define NRFUSION_CAPTURE32_EXPORTS
#endif
#if !defined(NRFUSION_CAPTURE32_STATIC)
#include "nrfusion/CaptureD3D11.hpp"
#endif
#include "nrfusion/CaptureProvider32Export.h"

static std::unique_ptr<nrfusion::CaptureProvider32> g_client;

static nrfusion::CaptureProvider32& GetClient() {
    if (!g_client) {
        g_client = std::make_unique<nrfusion::CaptureProvider32>();
    }
    return *g_client;
}

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_Connect(uint32_t hostPid, uint32_t timeoutMs) {
    return GetClient().Connect(hostPid, timeoutMs);
}

NRFUSION_CAPTURE32_API void __cdecl NRFusion_Capture32_Disconnect() {
    if (g_client) {
        g_client->Disconnect();
    }
}

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_IsConnected() {
    return g_client ? g_client->IsConnected() : false;
}

NRFUSION_CAPTURE32_API uint64_t __cdecl NRFusion_Capture32_ActiveSessionId() {
    return g_client ? g_client->ActiveSessionId() : 0;
}

NRFUSION_CAPTURE32_API void __cdecl NRFusion_Capture32_SetWorkingScale(float workingScale) {
#if !defined(NRFUSION_CAPTURE32_STATIC)
    nrfusion::SetCaptureD3D11WorkingScale(workingScale);
#else
    (void) workingScale;
#endif
}

NRFUSION_CAPTURE32_API void __cdecl NRFusion_Capture32_SetProcessingMode(uint32_t mode) {
#if !defined(NRFUSION_CAPTURE32_STATIC)
    nrfusion::SetCaptureD3D11ProcessingMode(mode);
#else
    (void) mode;
#endif
}

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_GetTransportInfo(
    nrfusion::CaptureD3D11TransportInfo* transportInfo) {
    if (!transportInfo) return false;
#if !defined(NRFUSION_CAPTURE32_STATIC)
    return nrfusion::GetCaptureD3D11TransportInfo(*transportInfo);
#else
    *transportInfo = {};
    return false;
#endif
}

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_Configure(const nrfusion::CaptureClientConfig* config) {
    if (!config) return false;
    return GetClient().Configure(*config);
}

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_SubmitFramePipelined(uint64_t workId,
                                                                         uint64_t producerFenceValue,
                                                                         float jitterX, float jitterY,
                                                                         bool reset,
                                                                         nrfusion::PipelinedFrameResult* outResult) {
    if (!outResult) return false;
    return GetClient().SubmitFramePipelined(workId, producerFenceValue, jitterX, jitterY, reset, *outResult);
}

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_SubmitFramePipelinedEx(uint64_t workId,
                                                                                uint64_t featureId,
                                                                                uint64_t viewId,
                                                                                uint64_t producerFenceValue,
                                                                                uint64_t consumerFenceValue,
                                                                                float jitterX,
                                                                                float jitterY,
                                                                                bool reset,
                                                                                nrfusion::PipelinedFrameResult* outResult) {
    if (!outResult) return false;
    return GetClient().SubmitFramePipelinedEx(workId, featureId, viewId, producerFenceValue,
                                               consumerFenceValue, jitterX, jitterY, reset, *outResult);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
#if !defined(NRFUSION_CAPTURE32_STATIC)
        nrfusion::StartCaptureD3D11Hooks();
#endif
        break;
    case DLL_PROCESS_DETACH:
#if !defined(NRFUSION_CAPTURE32_STATIC)
        nrfusion::StopCaptureD3D11Hooks();
#endif
        if (lpvReserved == nullptr) {
            if (g_client) {
                g_client->Disconnect();
                g_client.reset();
            }
        }
        break;
    }
    return TRUE;
}
