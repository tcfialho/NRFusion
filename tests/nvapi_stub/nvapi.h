#pragma once

#include <cstdint>

struct ID3D12GraphicsCommandList;

using NvU32 = std::uint32_t;
using NVDX_ObjectHandle = void*;

enum NvAPI_Status : int {
    NVAPI_OK = 0,
    NVAPI_ERROR = -1
};

struct NVAPI_DIM3 {
    NvU32 x = 0;
    NvU32 y = 0;
    NvU32 z = 0;
};

struct NVAPI_CU_KERNEL_LAUNCH_PARAMS {
    NVDX_ObjectHandle hFunction = nullptr;
    NVAPI_DIM3 gridDim{};
    NVAPI_DIM3 blockDim{};
    NvU32 dynSharedMemBytes = 0;
    const void* pParams = nullptr;
    NvU32 paramSize = 0;
};

NvAPI_Status NvAPI_D3D12_LaunchCuKernelChain(
    ID3D12GraphicsCommandList* commands,
    const NVAPI_CU_KERNEL_LAUNCH_PARAMS* kernels,
    NvU32 count);
