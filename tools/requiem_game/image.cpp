#include "image.hpp"

#include <wincodec.h>
#include <windows.h>

#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

namespace requiem {
namespace {

// The component has to be initialised once per process, and the testbed may be the only thing
// that ever asks for it. Apartment-threaded is what the imaging component expects.
struct ComGuard {
    ComGuard() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
};

} // namespace

std::wstring FindAsset(const wchar_t* name) {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    std::wstring base = executable;
    const size_t slash = base.find_last_of(L'\\');
    if (slash != std::wstring::npos) base.resize(slash + 1);

    const std::wstring candidates[] = {
        base + name,
        base + L"assets\\" + name,
        base + L"..\\..\\tools\\requiem_game\\assets\\" + name,
        std::wstring(L"tools\\requiem_game\\assets\\") + name,
    };
    for (const auto& candidate : candidates)
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
            return candidate;
    return {};
}

Image LoadImage(ID3D12Device* device, ID3D12GraphicsCommandList* commands, const wchar_t* path,
                ComPtr<ID3D12Resource>& keepAlive) {
    static ComGuard guard;
    Image image;
    if (!path || !*path) {
        image.status = "arquivo nao encontrado";
        return image;
    }

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) {
        image.status = "componente de imagem do Windows indisponivel";
        return image;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder))) {
        image.status = "nao consegui decodificar a imagem";
        return image;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        image.status = "nao consegui converter para RGBA";
        return image;
    }
    UINT width = 0, height = 0;
    converter->GetSize(&width, &height);
    std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4,
                                     static_cast<UINT>(pixels.size()), pixels.data()))) {
        image.status = "nao consegui ler os pixels";
        return image;
    }

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&image.texture)))) {
        image.status = "nao consegui criar a textura";
        return image;
    }

    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rows = 0;
    UINT64 rowBytes = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, &rows, &rowBytes, &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&keepAlive)))) {
        image.texture.Reset();
        image.status = "nao consegui criar o buffer de envio";
        return image;
    }

    std::uint8_t* mapped = nullptr;
    D3D12_RANGE none{0, 0};
    keepAlive->Map(0, &none, reinterpret_cast<void**>(&mapped));
    for (UINT row = 0; row < rows; ++row)
        memcpy(mapped + layout.Offset + static_cast<size_t>(row) * layout.Footprint.RowPitch,
               pixels.data() + static_cast<size_t>(row) * width * 4, static_cast<size_t>(rowBytes));
    keepAlive->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = image.texture.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = keepAlive.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = layout;
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = image.texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &barrier);

    image.width = width;
    image.height = height;
    image.status = "carregada";
    return image;
}

} // namespace requiem
