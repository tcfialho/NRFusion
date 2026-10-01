#pragma once
#include "NrKernelProfileD3D12.hpp"
#include <dxgi.h>
#include <nvapi.h>

namespace nrfusion::kernelprofile {
void PrepareReplacement(ID3D12Device* device, const FunctionIdentity& identity,
    decltype(&NvAPI_D3D12_CreateCuModule) createModule,
    decltype(&NvAPI_D3D12_CreateCuFunction) createFunction,
    decltype(&NvAPI_D3D12_DestroyCuModule) destroyModule);
void DeactivateReplacements(ID3D12Device* device);
void ForgetReplacement(ID3D12Device* device, NVDX_ObjectHandle function,
    decltype(&NvAPI_D3D12_DestroyCuFunction) destroyFunction,
    decltype(&NvAPI_D3D12_DestroyCuModule) destroyModule);
bool SelectReplacement(const NVAPI_CU_KERNEL_LAUNCH_PARAMS& stock,
    NVAPI_CU_KERNEL_LAUNCH_PARAMS& selected) noexcept;
}
