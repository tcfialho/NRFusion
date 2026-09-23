#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/CaptureProvider32Export.h"

#include <array>
#include <cstdint>

using Microsoft::WRL::ComPtr;

struct SpawnedHost {
    HANDLE process = nullptr;
    void Stop();
    ~SpawnedHost();
};

bool Fail(const char* message);
bool StartHost(SpawnedHost& host);
bool CreateSharedTexture(
    ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
    bool allowUav, ComPtr<ID3D11Texture2D>& texture,
    ComPtr<IDXGIKeyedMutex>& keyedMutex, HANDLE& sharedHandle);
bool CreateSharedTexture(
    ID3D11Device* device, UINT width, UINT height,
    ComPtr<ID3D11Texture2D>& texture,
    ComPtr<IDXGIKeyedMutex>& keyedMutex, HANDLE& sharedHandle);
bool CreateSharedFence(
    ID3D11Device5* device, ComPtr<ID3D11Fence>& fence, HANDLE& sharedHandle);
bool ReadFirstPixelForTest(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* source, std::array<std::uint8_t, 4>& pixel);
bool ReadFirstHalfPixelForTest(
    ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Texture2D* source, std::array<std::uint16_t, 4>& pixel);
bool SubmitUntilAccepted(
    std::uint64_t workId, std::uint64_t producerValue,
    std::uint64_t consumerValue, nrfusion::PipelinedFrameResult& result);

bool RunNeuralRoundtrip(
    SpawnedHost& host,
    const ComPtr<ID3D11Device>& device,
    const ComPtr<ID3D11Device5>& device5,
    const ComPtr<ID3D11DeviceContext>& context,
    const ComPtr<ID3D11DeviceContext4>& context4);
bool RunReducedRoundtrip(
    SpawnedHost& host,
    const ComPtr<ID3D11Device>& device,
    const ComPtr<ID3D11Device5>& device5,
    const ComPtr<ID3D11DeviceContext>& context,
    const ComPtr<ID3D11DeviceContext4>& context4);
