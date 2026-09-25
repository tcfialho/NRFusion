#include "nrfusion/D3D9ExCarrierAcquire.hpp"

#include <cmath>
#include <wrl/client.h>

namespace nrfusion {

D3D9ExNativeAcquireResult AcquireD3D9ExNativeFrame(
    const D3D9ExNativeAcquireInput& input) noexcept {
    if (input.identity.frameId == 0 ||
        input.identity.configurationGeneration == 0) {
        return {{},
            D3D9ExNativeAcquireFailure::InvalidIdentity};
    }
    if (!input.device)
        return {{}, D3D9ExNativeAcquireFailure::MissingDevice};
    if (!input.color)
        return {{}, D3D9ExNativeAcquireFailure::MissingColor};
    if (!std::isfinite(input.jitter.x) ||
        !std::isfinite(input.jitter.y)) {
        return {{}, D3D9ExNativeAcquireFailure::InvalidSurface};
    }

    Microsoft::WRL::ComPtr<IDirect3DDevice9> colorDevice;
    Microsoft::WRL::ComPtr<IDirect3DDevice9> expectedDevice;
    if (FAILED(input.color->GetDevice(&colorDevice)) ||
        FAILED(input.device->QueryInterface(
            IID_PPV_ARGS(&expectedDevice))) ||
        !colorDevice || !expectedDevice ||
        colorDevice.Get() != expectedDevice.Get()) {
        return {{}, D3D9ExNativeAcquireFailure::DeviceMismatch};
    }

    D3DSURFACE_DESC desc{};
    if (FAILED(input.color->GetDesc(&desc)) ||
        desc.Width == 0 || desc.Height == 0 ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        return {{}, D3D9ExNativeAcquireFailure::InvalidSurface};
    }
    if (desc.Format != D3DFMT_A16B16G16R16F) {
        return {{},
            D3D9ExNativeAcquireFailure::UnsupportedFormat};
    }
    if (desc.Pool != D3DPOOL_DEFAULT) {
        return {{},
            D3D9ExNativeAcquireFailure::UnsupportedPool};
    }

    FrameContext frame{};
    frame.frameId = input.identity.frameId;
    frame.hostFrameToken = input.identity.hostFrameToken;
    frame.viewId = input.identity.viewId;
    frame.configurationGeneration =
        input.identity.configurationGeneration;
    frame.color.opaqueId =
        reinterpret_cast<std::uint64_t>(input.color);
    frame.color.resolution = {desc.Width, desc.Height};
    frame.color.format = ResourceFormat::Rgba16Float;
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
    frame.api = GraphicsApi::D3D9;
    return {frame, D3D9ExNativeAcquireFailure::None};
}

} // namespace nrfusion
