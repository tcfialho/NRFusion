#include "D3D9ExShareHarness.hpp"

namespace nrfusion::test {

bool D3D9ExShareHarness::CreateD3D11NtBridge(
    std::uint32_t width,
    std::uint32_t height) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE |
        D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    if (FAILED(device11_->CreateTexture2D(
            &desc, nullptr, &ntBridge11_)) ||
        FAILED(ntBridge11_.As(&ntMutex11_))) {
        return false;
    }

    ComPtr<IDXGIResource1> resource;
    HANDLE ntHandle = nullptr;
    if (FAILED(ntBridge11_.As(&resource)) ||
        FAILED(resource->CreateSharedHandle(
            nullptr, GENERIC_ALL, nullptr, &ntHandle))) {
        return false;
    }

    const HRESULT opened = device12_->OpenSharedHandle(
        ntHandle, IID_PPV_ARGS(&ntBridge12_));
    CloseHandle(ntHandle);
    if (FAILED(opened) || !ntBridge12_) return false;

    const D3D12_RESOURCE_DESC openedDesc =
        ntBridge12_->GetDesc();
    return openedDesc.Dimension ==
               D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
           openedDesc.Width == width &&
           openedDesc.Height == height &&
           openedDesc.Format ==
               DXGI_FORMAT_R16G16B16A16_FLOAT;
}

bool D3D9ExShareHarness::ProveSharedTexture(
    std::uint32_t width,
    std::uint32_t height) {
    if (!device9_ || !device11_ || !device12_ ||
        width == 0 || height == 0) {
        return false;
    }

    sharedTexture11_.Reset();
    sharedTexture9_.Reset();
    eventQuery9_.Reset();
    ntBridge12_.Reset();
    ntMutex11_.Reset();
    ntBridge11_.Reset();
    sharedHandle9_ = nullptr;

    HANDLE shared = nullptr;
    if (FAILED(device9_->CreateTexture(
            width, height, 1,
            D3DUSAGE_RENDERTARGET,
            D3DFMT_A16B16G16R16F,
            D3DPOOL_DEFAULT,
            &sharedTexture9_,
            &shared)) ||
        !sharedTexture9_ || !shared) {
        return false;
    }
    sharedHandle9_ = shared;

    if (FAILED(device11_->OpenSharedResource(
            sharedHandle9_,
            IID_PPV_ARGS(&sharedTexture11_))) ||
        !sharedTexture11_) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc11{};
    sharedTexture11_->GetDesc(&desc11);
    if (desc11.Width != width ||
        desc11.Height != height ||
        desc11.MipLevels != 1 ||
        desc11.ArraySize != 1 ||
        desc11.Format !=
            DXGI_FORMAT_R16G16B16A16_FLOAT ||
        desc11.SampleDesc.Count != 1) {
        return false;
    }

    if (FAILED(device9_->CreateQuery(
            D3DQUERYTYPE_EVENT,
            &eventQuery9_)) ||
        !eventQuery9_ ||
        FAILED(eventQuery9_->Issue(D3DISSUE_END))) {
        return false;
    }

    const HRESULT queryState =
        eventQuery9_->GetData(nullptr, 0, 0);
    if (queryState != S_OK && queryState != S_FALSE)
        return false;

    if (!CreateD3D11NtBridge(width, height))
        return false;
    if (!fenceBridge_.QueueInputHandoff() ||
        !fenceBridge_.QueueOutputHandoff()) {
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

} // namespace nrfusion::test
