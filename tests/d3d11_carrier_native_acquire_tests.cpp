#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include "nrfusion/D3D11CarrierNativeAcquire.hpp"

#include <cassert>
#include <limits>

using Microsoft::WRL::ComPtr;
using namespace nrfusion;

namespace {

bool CreateDevice(
    ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& context) {
    D3D_FEATURE_LEVEL feature{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &feature, &context);
    if (SUCCEEDED(hr)) return true;
    char requireHardware[2]{};
    if (GetEnvironmentVariableA(
            "NRFUSION_TEST_D3D11_HARDWARE", requireHardware,
            static_cast<DWORD>(sizeof(requireHardware))) != 0)
        return false;
    return SUCCEEDED(D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, &feature, &context));
}

ComPtr<ID3D11Texture2D> Texture(
    ID3D11Device* device, DXGI_FORMAT format, UINT samples = 1) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 96;
    desc.Height = 64;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = samples;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    assert(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
    return texture;
}

D3D11NativeAcquireInput Input(
    ID3D11DeviceContext* context, ID3D11Resource* color) {
    D3D11NativeAcquireInput input{};
    input.identity.frameId = 11;
    input.identity.hostFrameToken = 22;
    input.identity.viewId = 3;
    input.identity.configurationGeneration = 7;
    input.context = context;
    input.color = color;
    input.jitter = {0.25f, -0.25f};
    return input;
}

} // namespace

int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    assert(CreateDevice(device, context));

    auto color = Texture(device.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
    auto input = Input(context.Get(), color.Get());
    input.hdr = true;
    const auto acquired = AcquireD3D11NativeFrame(input);
    assert(acquired);
    assert(acquired.frame.ReadyForCore());
    assert(acquired.frame.api == GraphicsApi::D3D11);
    assert(acquired.frame.color.provenance == ResourceProvenance::GameNative);
    assert(acquired.frame.color.reliability == ResourceReliability::Reliable);
    assert(acquired.frame.color.ownership == ResourceOwnership::Borrowed);
    assert(acquired.frame.color.lifetime == ResourceLifetime::Frame);
    assert(acquired.frame.color.sourceFrameId == input.identity.frameId);
    const Resolution expectedResolution{96, 64};
    assert(acquired.frame.renderResolution == expectedResolution);
    assert(acquired.frame.outputResolution == expectedResolution);
    assert(acquired.frame.hdr);

    auto badIdentity = input;
    badIdentity.identity.frameId = 0;
    assert(AcquireD3D11NativeFrame(badIdentity).failure ==
           D3D11NativeAcquireFailure::InvalidIdentity);
    auto missingContext = input;
    missingContext.context = nullptr;
    assert(AcquireD3D11NativeFrame(missingContext).failure ==
           D3D11NativeAcquireFailure::MissingContext);
    auto rgba8 = Texture(device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    const auto rgba8Acquire =
        AcquireD3D11NativeFrame(Input(context.Get(), rgba8.Get()));
    assert(rgba8Acquire);
    assert(rgba8Acquire.frame.color.format == ResourceFormat::Rgba8Unorm);

    auto unsupported = Texture(device.Get(), DXGI_FORMAT_B8G8R8A8_UNORM);
    assert(AcquireD3D11NativeFrame(Input(context.Get(), unsupported.Get())).failure ==
           D3D11NativeAcquireFailure::UnsupportedFormat);
    auto msaa = Texture(device.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 2);
    assert(AcquireD3D11NativeFrame(Input(context.Get(), msaa.Get())).failure ==
           D3D11NativeAcquireFailure::InvalidTexture);

    ComPtr<ID3D11Device> otherDevice;
    ComPtr<ID3D11DeviceContext> otherContext;
    assert(CreateDevice(otherDevice, otherContext));
    assert(AcquireD3D11NativeFrame(Input(otherContext.Get(), color.Get())).failure ==
           D3D11NativeAcquireFailure::DeviceMismatch);

    auto nanJitter = input;
    nanJitter.jitter.x = (std::numeric_limits<float>::quiet_NaN)();
    assert(AcquireD3D11NativeFrame(nanJitter).failure ==
           D3D11NativeAcquireFailure::InvalidTexture);
    return 0;
}
