#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>

#include "nrfusion/NrDeferredRetirementQueue.hpp"

namespace nrfusion {

enum class D3D12NrScratchKind : std::uint8_t {
    Output,
    ColorCopy,
    HdrCopy
};

struct D3D12NrScratchDesc {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t frameWidth = 0;
    std::uint32_t frameHeight = 0;
    std::uint32_t workWidth = 0;
    std::uint32_t workHeight = 0;

    bool operator==(const D3D12NrScratchDesc&) const noexcept = default;
};

class D3D12NrScratchResources {
public:
    bool Ensure(ID3D12Device* device, const D3D12NrScratchDesc& desc,
                NrDeferredRetirementQueue& retirement) noexcept;
    bool Retire(NrDeferredRetirementQueue& retirement) noexcept;
    bool Transition(ID3D12GraphicsCommandList* cmdList, D3D12NrScratchKind kind,
                    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept;
    void ReleaseAfterIdle() noexcept;

    bool Complete() const noexcept;
    bool Matches(const D3D12NrScratchDesc& desc) const noexcept;
    ID3D12Resource* Get(D3D12NrScratchKind kind) const noexcept;
    D3D12_RESOURCE_STATES State(D3D12NrScratchKind kind) const noexcept;

private:
    struct Surface {
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    };

    static ID3D12Resource* Create(ID3D12Device* device, DXGI_FORMAT format,
                                  std::uint32_t width, std::uint32_t height) noexcept;
    static void Release(Surface& surface) noexcept;
    static bool Park(Surface& surface, NrDeferredRetirementQueue& retirement) noexcept;

    Surface& Slot(D3D12NrScratchKind kind) noexcept;
    const Surface& Slot(D3D12NrScratchKind kind) const noexcept;
    std::size_t ActiveCount() const noexcept;

    Surface output_{};
    Surface colorCopy_{};
    Surface hdrCopy_{};
    D3D12NrScratchDesc desc_{};
};

} // namespace nrfusion
