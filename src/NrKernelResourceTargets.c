#define CINTERFACE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

void NRFusion_ResourceCreationTargets(ID3D12Device* device, void** targets) {
    targets[0] = (void*)device->lpVtbl->CreateCommittedResource;
    targets[1] = (void*)device->lpVtbl->CreatePlacedResource;
    targets[2] = (void*)device->lpVtbl->CreateReservedResource;
}

void* NRFusion_ResourceBarrierTarget(ID3D12GraphicsCommandList* commands) {
    return (void*)commands->lpVtbl->ResourceBarrier;
}
