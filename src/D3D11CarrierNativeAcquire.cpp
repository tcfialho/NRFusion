#include "nrfusion/D3D11CarrierNativeAcquire.hpp"

#include <cmath>

namespace nrfusion {
namespace {

ResourceFormat MapFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return ResourceFormat::Rgba8Unorm;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return ResourceFormat::Rgba16Float;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return ResourceFormat::Rgba32Float;
    default: return ResourceFormat::Unknown;
    }
}

} // namespace

D3D11NativeAcquireResult AcquireD3D11NativeFrame(
    const D3D11NativeAcquireInput& input) noexcept {
    if (input.identity.frameId == 0 ||
        input.identity.configurationGeneration == 0)
        return {{}, D3D11NativeAcquireFailure::InvalidIdentity};
    if (input.context == nullptr)
        return {{}, D3D11NativeAcquireFailure::MissingContext};
    if (input.color == nullptr)
        return {{}, D3D11NativeAcquireFailure::MissingColor};
    if (!std::isfinite(input.jitter.x) || !std::isfinite(input.jitter.y))
        return {{}, D3D11NativeAcquireFailure::InvalidTexture};

    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    Microsoft::WRL::ComPtr<ID3D11Device> colorDevice;
    input.context->GetDevice(&contextDevice);
    input.color->GetDevice(&colorDevice);
    if (!contextDevice || !colorDevice || contextDevice.Get() != colorDevice.Get())
        return {{}, D3D11NativeAcquireFailure::DeviceMismatch};

    Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
    if (FAILED(input.color->QueryInterface(IID_PPV_ARGS(&color))))
        return {{}, D3D11NativeAcquireFailure::InvalidTexture};

    D3D11_TEXTURE2D_DESC desc{};
    color->GetDesc(&desc);
    if (desc.Width == 0 || desc.Height == 0 || desc.ArraySize != 1 ||
        desc.MipLevels != 1 || desc.SampleDesc.Count != 1)
        return {{}, D3D11NativeAcquireFailure::InvalidTexture};
    const ResourceFormat format = MapFormat(desc.Format);
    if (format == ResourceFormat::Unknown)
        return {{}, D3D11NativeAcquireFailure::UnsupportedFormat};

    FrameContext frame{};
    frame.frameId = input.identity.frameId;
    frame.hostFrameToken = input.identity.hostFrameToken;
    frame.viewId = input.identity.viewId;
    frame.configurationGeneration = input.identity.configurationGeneration;
    frame.color.opaqueId = reinterpret_cast<std::uint64_t>(color.Get());
    frame.color.resolution = {desc.Width, desc.Height};
    frame.color.format = format;
    frame.color.provenance = ResourceProvenance::GameNative;
    frame.color.reliability = ResourceReliability::Reliable;
    frame.color.ownership = ResourceOwnership::Borrowed;
    frame.color.lifetime = ResourceLifetime::Frame;
    frame.color.sourceFrameId = input.identity.frameId;
    frame.renderResolution = frame.color.resolution;
    frame.outputResolution = frame.color.resolution;
    frame.jitter = input.jitter;
    frame.hdr = input.hdr;
    frame.cameraCut = input.cameraCut;
    frame.resetHistory = input.resetHistory;
    frame.api = GraphicsApi::D3D11;
    return {frame, D3D11NativeAcquireFailure::None};
}

} // namespace nrfusion
