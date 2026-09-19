#pragma once

#include "nrfusion/Types.hpp"
#include "nrfusion/WorkLedger.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace nrfusion {

struct SyntheticFrameInputs {
    WorkTicket ticket{};
    FrameId frameId = 0;

    ResourceRef color{};
    ResourceRef depth{};
    ResourceRef motionVectors{};
    ResourceRef exposure{};
    ResourceRef reactiveMask{};

    Resolution renderResolution{};
    Resolution targetResolution{};
    Jitter jitter{};

    float motionScaleX = 1.0f;
    float motionScaleY = 1.0f;
    float workingScale = 1.0f;
    float preExposure = 1.0f;

    bool reset = false;
    bool depthInverted = true;
    bool hdr = false;
    MotionSource motionSource = MotionSource::Native;
    bool cameraCut = false;

    constexpr bool Valid() const noexcept {
        return ticket.id != 0 && color.Valid() && renderResolution.Valid();
    }
};

struct ProviderContext {
    GraphicsApi api = GraphicsApi::Unknown;
    void* device = nullptr;         // ID3D12Device* for D3D12, ID3D11Device* for D3D11
    void* commandQueue = nullptr;   // ID3D12CommandQueue* for D3D12
    bool is32Bit = false;
    bool preferSameDevice = true;
    bool enableAsyncCompute = false;
};

struct SyntheticWorkHandle {
    uint64_t workId = 0;
    uint64_t featureId = 0;
    uint64_t viewId = 0;
    uint64_t fenceValue = 0;
    bool valid = false;
    bool completed = false;
    float workingScale = 1.0f;
    Resolution workResolution{};
    Resolution nativeResolution{};
};

class ISyntheticProvider {
public:
    virtual ~ISyntheticProvider() = default;

    virtual bool Initialize(const ProviderContext& context) = 0;
    virtual void Shutdown() = 0;
    virtual bool IsReady() const noexcept = 0;

    // Submits the synthetic DLAA frame. If workingScale < 1.0, performs GPU downsample.
    // Zero CPU copy: all resources remain on the GPU.
    virtual SyntheticWorkHandle Submit(const SyntheticFrameInputs& inputs, void* commandList) = 0;

    // Polls or checks whether the work associated with handle is complete on the GPU.
    virtual bool Poll(const SyntheticWorkHandle& handle) = 0;

    // Retrieves the extracted residual texture (Low-res or native).
    virtual ResourceRef GetResidual(const SyntheticWorkHandle& handle) = 0;

    // Composes Native Result = Original Native + Upscaled Residual on GPU command list.
    virtual bool ComposeNative(const SyntheticWorkHandle& handle,
                               const ResourceRef& originalNative,
                               const ResourceRef& destinationNative,
                               void* commandList,
                               float residualWeight = 1.0f) = 0;

    virtual const char* Name() const noexcept = 0;
};

} // namespace nrfusion
