#pragma once
#include <dxgi1_6.h>
#include <d3d12.h>
#include <nvapi.h>
#include <string>

namespace nrfusion {
void NoteNrFunction(NVDX_ObjectHandle function, const char* name);
// Which kernels the installed neural runtime actually registers. Any path that targets a specific
// kernel is a guess until this is read on the machine that will run it.
std::string NrFunctionNamesSummary();
// Name -> parameter-block size and launch count, the evidence for which family carries which layer.
std::string NrLaunchShapeSummary();
void ForgetNrFunction(NVDX_ObjectHandle function);
NvAPI_Status ProfileNrChain(ID3D12GraphicsCommandList* commands,
    const NVAPI_CU_KERNEL_LAUNCH_PARAMS* kernels, NvU32 count,
    decltype(&NvAPI_D3D12_LaunchCuKernelChain) original);

class NrPassDiagnosticScope {
public:
    explicit NrPassDiagnosticScope(ID3D12GraphicsCommandList* commands);
    ~NrPassDiagnosticScope();
    void Succeeded() { successful_ = true; }
private:
    ID3D12GraphicsCommandList* commands_ = nullptr;
    unsigned query_ = 0;
    bool successful_ = false;
};
} // namespace nrfusion
