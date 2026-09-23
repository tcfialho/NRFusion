#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <array>

#include "nrfusion/D3D12CarrierExecutionPlan.hpp"
#include "nrfusion/D3D12CarrierNativeAcquire.hpp"
#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

struct D3D12CarrierExecuteOptions {
    D3D12CarrierExecutionConfig execution{};
    std::array<DlssNrTuning, kD3D12NrMaxPassCount> tuning{};
    D3D12NrComposition composition{};
    D3D12_RESOURCE_STATES colorState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES outputState =
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES depthState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES motionState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_STATES exposureState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
};

enum class D3D12CarrierExecuteFailure : std::uint8_t {
    None,
    Planning,
    InvalidCommandList,
    ResourceIdentityMismatch,
    MissingExposure,
    ExecutorFailed
};

struct D3D12CarrierExecuteResult {
    D3D12CarrierExecuteFailure failure = D3D12CarrierExecuteFailure::None;
    D3D12CarrierExecutionFailure planningFailure =
        D3D12CarrierExecutionFailure::None;
    D3D12NrFrameResult executorResult = D3D12NrFrameResult::Failed;
    bool attempted = false;

    constexpr explicit operator bool() const noexcept {
        return attempted && failure == D3D12CarrierExecuteFailure::None &&
               executorResult == D3D12NrFrameResult::Applied;
    }
};

class D3D12CarrierExecutor {
public:
    bool Load() { return executor_.Load(); }
    bool BindDeviceAfterIdle(ID3D12Device* device);
    void ShutdownAfterIdle();

    D3D12CarrierExecuteResult Execute(
        ID3D12GraphicsCommandList* cmdList,
        const D3D12NativeFrameResources& resources,
        const D3D12CarrierFrameResult& frame,
        const D3D12CarrierWork& work,
        const D3D12CarrierExecuteOptions& options = {});

    const D3D12NrExecutor& Executor() const noexcept { return executor_; }

private:
    D3D12NrExecutor executor_{};
    ID3D12Device* boundDevice_ = nullptr;
};

} // namespace nrfusion
