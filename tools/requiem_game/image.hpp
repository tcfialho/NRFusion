#pragma once

// Load a picture with the Windows imaging component and hand it to Direct3D 12.
//
// The testbed shows the NVIDIA reference frame, not a synthetic scene, and the difference
// matters for what it is measuring: a neural pass fed flat-shaded geometry sees edges and
// gradients it never meets in a game, so any conclusion drawn from that input is about the
// wrong picture. The reference frame is photographic content with real grain and real detail.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>

namespace requiem {

struct Image {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string status = "nao carregada";

    bool Valid() const noexcept { return texture != nullptr; }
};

// Decodes to RGBA8 and uploads through a temporary buffer. The upload buffer is kept alive by
// the caller's command list until it has executed, which is why it comes back in `keepAlive`.
Image LoadImage(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                const wchar_t* path, Microsoft::WRL::ComPtr<ID3D12Resource>& keepAlive);

// Looks beside the executable first, then in the source tree, so the testbed works both from
// the package and from a build directory without anyone having to copy files by hand.
std::wstring FindAsset(const wchar_t* name);

} // namespace requiem
