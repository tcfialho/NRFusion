#pragma once
struct ID3D12Device;
namespace nrfusion::kernelprofile {
bool SupportsSm89Device(ID3D12Device* device);
}
