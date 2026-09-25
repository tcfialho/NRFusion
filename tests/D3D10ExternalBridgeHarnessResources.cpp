#include "D3D10ExternalBridgeHarness.hpp"

#include <DirectXPackedVector.h>

#include <iostream>

namespace nrfusion::test {
namespace {

D3D10_TEXTURE2D_DESC Texture10Desc(
    std::uint32_t width,
    std::uint32_t height,
    UINT miscFlags = 0) noexcept {
    D3D10_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags =
        D3D10_BIND_SHADER_RESOURCE |
        D3D10_BIND_RENDER_TARGET;
    desc.MiscFlags = miscFlags;
    return desc;
}

bool LegacyHandle(
    ID3D10Texture2D* texture,
    HANDLE& handle,
    ComPtr<IDXGIKeyedMutex>& mutex) {
    if (!texture) return false;
    ComPtr<IDXGIResource> resource;
    if (FAILED(texture->QueryInterface(IID_PPV_ARGS(&resource))) ||
        FAILED(texture->QueryInterface(IID_PPV_ARGS(&mutex))) ||
        FAILED(resource->GetSharedHandle(&handle))) {
        return false;
    }
    return handle != nullptr && mutex != nullptr;
}

bool OpenLegacy11(
    ID3D11Device* device,
    HANDLE handle,
    ComPtr<ID3D11Texture2D>& texture,
    ComPtr<IDXGIKeyedMutex>& mutex) {
    if (!device || !handle) return false;
    if (FAILED(device->OpenSharedResource(
            handle, IID_PPV_ARGS(&texture))) ||
        FAILED(texture.As(&mutex))) {
        return false;
    }
    return true;
}

} // namespace

bool D3D10ExternalBridgeHarness::CreateResources(
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t seed,
    D3D10BridgeCycleResources& resources) {
    if (!device10_ || !device11_ || !device12_ ||
        width == 0 || height == 0) {
        std::cerr << "d3d10 bridge: invalid resource context\n";
        return false;
    }

    resources = {};
    resources.width = width;
    resources.height = height;
    const std::size_t pixels =
        static_cast<std::size_t>(width) * height;
    resources.expected.resize(pixels * 4);

    const float base =
        static_cast<float>((seed % 7) + 1) / 8.0f;
    const float values[4] = {
        base, 1.0f - base, base * 0.5f, 1.0f};
    std::uint16_t packed[4]{};
    for (std::size_t channel = 0; channel != 4; ++channel) {
        packed[channel] =
            DirectX::PackedVector::XMConvertFloatToHalf(
                values[channel]);
    }
    for (std::size_t pixel = 0; pixel != pixels; ++pixel) {
        for (std::size_t channel = 0; channel != 4; ++channel) {
            resources.expected[pixel * 4 + channel] =
                packed[channel];
        }
    }

    const auto baseDesc = Texture10Desc(width, height);
    D3D10_SUBRESOURCE_DATA initial{};
    initial.pSysMem = resources.expected.data();
    initial.SysMemPitch = width * 4 * sizeof(std::uint16_t);
    if (FAILED(device10_->CreateTexture2D(
            &baseDesc, &initial, &resources.source10)) ||
        FAILED(device10_->CreateTexture2D(
            &baseDesc, nullptr, &resources.destination10))) {
        std::cerr << "d3d10 bridge: source/destination create failed\n";
        return false;
    }

    auto readbackDesc = baseDesc;
    readbackDesc.Usage = D3D10_USAGE_STAGING;
    readbackDesc.BindFlags = 0;
    readbackDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    if (FAILED(device10_->CreateTexture2D(
            &readbackDesc, nullptr, &resources.readback10))) {
        std::cerr << "d3d10 bridge: readback create failed\n";
        return false;
    }

    const auto legacyDesc = Texture10Desc(
        width, height,
        D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX);
    if (FAILED(device10_->CreateTexture2D(
            &legacyDesc, nullptr, &resources.legacyInput10)) ||
        FAILED(device10_->CreateTexture2D(
            &legacyDesc, nullptr, &resources.legacyOutput10))) {
        std::cerr << "d3d10 bridge: legacy texture create failed\n";
        return false;
    }

    HANDLE inputLegacyHandle = nullptr;
    HANDLE outputLegacyHandle = nullptr;
    if (!LegacyHandle(
            resources.legacyInput10.Get(),
            inputLegacyHandle,
            resources.inputMutex10) ||
        !LegacyHandle(
            resources.legacyOutput10.Get(),
            outputLegacyHandle,
            resources.outputMutex10) ||
        !OpenLegacy11(
            device11_.Get(),
            inputLegacyHandle,
            resources.legacyInput11,
            resources.inputMutex11) ||
        !OpenLegacy11(
            device11_.Get(),
            outputLegacyHandle,
            resources.legacyOutput11,
            resources.outputMutex11)) {
        std::cerr << "d3d10 bridge: legacy handle/open failed\n";
        return false;
    }

    D3D11_TEXTURE2D_DESC shared11{};
    shared11.Width = width;
    shared11.Height = height;
    shared11.MipLevels = 1;
    shared11.ArraySize = 1;
    shared11.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    shared11.SampleDesc.Count = 1;
    shared11.Usage = D3D11_USAGE_DEFAULT;
    shared11.BindFlags =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;
    shared11.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (FAILED(device11_->CreateTexture2D(
            &shared11, nullptr, &resources.ntShared11)) ||
        FAILED(resources.ntShared11.As(&resources.ntMutex11))) {
        std::cerr << "d3d10 bridge: D3D11 NT texture/mutex create failed\n";
        return false;
    }

    ComPtr<IDXGIResource1> ntResource;
    HANDLE ntHandle = nullptr;
    if (FAILED(resources.ntShared11.As(&ntResource)) ||
        FAILED(ntResource->CreateSharedHandle(
            nullptr, GENERIC_ALL, nullptr, &ntHandle))) {
        std::cerr << "d3d10 bridge: D3D11 NT handle create failed\n";
        return false;
    }
    const HRESULT opened = device12_->OpenSharedHandle(
        ntHandle, IID_PPV_ARGS(&resources.ntShared12));
    CloseHandle(ntHandle);
    if (FAILED(opened) || !resources.ntShared12) {
        std::cerr << "d3d10 bridge: D3D12 NT open failed hr=0x"
                  << std::hex << static_cast<unsigned long>(opened)
                  << std::dec << "\n";
        return false;
    }

    const D3D12_RESOURCE_DESC openedDesc =
        resources.ntShared12->GetDesc();
    const bool valid =
        openedDesc.Dimension ==
            D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        openedDesc.Width == width &&
        openedDesc.Height == height &&
        openedDesc.Format ==
            DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (!valid)
        std::cerr << "d3d10 bridge: opened D3D12 desc mismatch\n";
    return valid;
}

} // namespace nrfusion::test
