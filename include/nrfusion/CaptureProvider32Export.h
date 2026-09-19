#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "nrfusion/CaptureProvider32.hpp"
#include "nrfusion/CaptureD3D11.hpp"

#if defined(NRFUSION_CAPTURE32_STATIC)
#define NRFUSION_CAPTURE32_API extern "C"
#elif defined(NRFUSION_CAPTURE32_EXPORTS)
#define NRFUSION_CAPTURE32_API extern "C" __declspec(dllexport)
#else
#define NRFUSION_CAPTURE32_API extern "C" __declspec(dllimport)
#endif

NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_Connect(uint32_t hostPid, uint32_t timeoutMs);
NRFUSION_CAPTURE32_API void __cdecl NRFusion_Capture32_Disconnect();
NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_IsConnected();
NRFUSION_CAPTURE32_API uint64_t __cdecl NRFusion_Capture32_ActiveSessionId();
NRFUSION_CAPTURE32_API void __cdecl NRFusion_Capture32_SetWorkingScale(float workingScale);
NRFUSION_CAPTURE32_API void __cdecl NRFusion_Capture32_SetProcessingMode(uint32_t mode);
NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_GetTransportInfo(
    nrfusion::CaptureD3D11TransportInfo* transportInfo);
NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_Configure(const nrfusion::CaptureClientConfig* config);
NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_SubmitFramePipelined(uint64_t workId,
                                                                         uint64_t producerFenceValue,
                                                                         float jitterX, float jitterY,
                                                                         bool reset,
                                                                         nrfusion::PipelinedFrameResult* outResult);
NRFUSION_CAPTURE32_API bool __cdecl NRFusion_Capture32_SubmitFramePipelinedEx(uint64_t workId,
                                                                                uint64_t featureId,
                                                                                uint64_t viewId,
                                                                                uint64_t producerFenceValue,
                                                                                uint64_t consumerFenceValue,
                                                                                float jitterX,
                                                                                float jitterY,
                                                                                bool reset,
                                                                                nrfusion::PipelinedFrameResult* outResult);
