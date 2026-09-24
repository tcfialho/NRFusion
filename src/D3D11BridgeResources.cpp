#include "nrfusion/D3D11BridgeResources.hpp"

#include <wrl/client.h>

namespace nrfusion {

bool D3D11ResourcesCopyCompatible(
    ID3D11Resource* source, ID3D11Resource* destination,
    ID3D11Device* expectedDevice) noexcept {
    if (!source || !destination || !expectedDevice) return false;

    Microsoft::WRL::ComPtr<ID3D11Device> sourceDevice;
    Microsoft::WRL::ComPtr<ID3D11Device> destinationDevice;
    source->GetDevice(&sourceDevice);
    destination->GetDevice(&destinationDevice);
    if (sourceDevice.Get() != expectedDevice ||
        destinationDevice.Get() != expectedDevice)
        return false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> destinationTexture;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(&sourceTexture))) ||
        FAILED(destination->QueryInterface(IID_PPV_ARGS(&destinationTexture))))
        return false;

    D3D11_TEXTURE2D_DESC sourceDesc{};
    D3D11_TEXTURE2D_DESC destinationDesc{};
    sourceTexture->GetDesc(&sourceDesc);
    destinationTexture->GetDesc(&destinationDesc);
    return sourceDesc.Width == destinationDesc.Width &&
        sourceDesc.Height == destinationDesc.Height &&
        sourceDesc.MipLevels == destinationDesc.MipLevels &&
        sourceDesc.ArraySize == destinationDesc.ArraySize &&
        sourceDesc.Format == destinationDesc.Format &&
        sourceDesc.SampleDesc.Count == destinationDesc.SampleDesc.Count &&
        sourceDesc.SampleDesc.Quality == destinationDesc.SampleDesc.Quality;
}

} // namespace nrfusion
