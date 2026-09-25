#include "D3D9ExShareHarness.hpp"

namespace nrfusion::test {
namespace {

bool CreateD3D9Texture(
    IDirect3DDevice9Ex* device,
    std::uint32_t width,
    std::uint32_t height,
    ComPtr<IDirect3DTexture9>& texture,
    HANDLE* sharedHandle) {
    return device &&
           SUCCEEDED(device->CreateTexture(
               width, height, 1,
               D3DUSAGE_RENDERTARGET,
               D3DFMT_A16B16G16R16F,
               D3DPOOL_DEFAULT,
               &texture, sharedHandle)) &&
           texture != nullptr;
}

bool OpenD3D11Shared(
    ID3D11Device* device,
    HANDLE sharedHandle,
    std::uint32_t width,
    std::uint32_t height,
    ComPtr<ID3D11Texture2D>& texture) {
    if (!device || !sharedHandle ||
        FAILED(device->OpenSharedResource(
            sharedHandle,
            IID_PPV_ARGS(&texture))) ||
        !texture) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    return desc.Width == width &&
           desc.Height == height &&
           desc.MipLevels == 1 &&
           desc.ArraySize == 1 &&
           desc.Format ==
               DXGI_FORMAT_R16G16B16A16_FLOAT &&
           desc.SampleDesc.Count == 1;
}

} // namespace

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

    sharedOutput11_.Reset();
    sharedInput11_.Reset();
    sharedOutput9_.Reset();
    sharedInput9_.Reset();
    gameDestination9_.Reset();
    gameSource9_.Reset();
    ntBridge12_.Reset();
    ntMutex11_.Reset();
    ntBridge11_.Reset();
    sharedOutputHandle9_ = nullptr;
    sharedInputHandle9_ = nullptr;

    if (!CreateD3D9Texture(
            device9_.Get(), width, height,
            gameSource9_, nullptr) ||
        !CreateD3D9Texture(
            device9_.Get(), width, height,
            gameDestination9_, nullptr) ||
        !CreateD3D9Texture(
            device9_.Get(), width, height,
            sharedInput9_, &sharedInputHandle9_) ||
        !CreateD3D9Texture(
            device9_.Get(), width, height,
            sharedOutput9_, &sharedOutputHandle9_) ||
        !sharedInputHandle9_ || !sharedOutputHandle9_) {
        return false;
    }

    if (!OpenD3D11Shared(
            device11_.Get(), sharedInputHandle9_,
            width, height, sharedInput11_) ||
        !OpenD3D11Shared(
            device11_.Get(), sharedOutputHandle9_,
            width, height, sharedOutput11_) ||
        !CreateD3D11NtBridge(width, height)) {
        return false;
    }

    width_ = width;
    height_ = height;
    return true;
}

} // namespace nrfusion::test
