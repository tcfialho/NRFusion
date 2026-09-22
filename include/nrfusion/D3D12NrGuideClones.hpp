#pragma once

#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>

#include "nrfusion/NrDeferredRetirementQueue.hpp"

namespace nrfusion {

enum class D3D12NrGuideKind : std::uint8_t {
    Depth,
    Motion
};

class D3D12NrGuideClones {
public:
    D3D12NrGuideClones() = default;
    D3D12NrGuideClones(const D3D12NrGuideClones&) = delete;
    D3D12NrGuideClones& operator=(const D3D12NrGuideClones&) = delete;
    D3D12NrGuideClones(D3D12NrGuideClones&&) = delete;
    D3D12NrGuideClones& operator=(D3D12NrGuideClones&&) = delete;

    bool Ensure(ID3D12Device* device, D3D12NrGuideKind kind,
                ID3D12Resource* source, DXGI_FORMAT typedFormat,
                NrDeferredRetirementQueue& retirement) noexcept;
    bool Retire(D3D12NrGuideKind kind, NrDeferredRetirementQueue& retirement) noexcept;
    bool Retire(NrDeferredRetirementQueue& retirement) noexcept;
    bool Transition(ID3D12GraphicsCommandList* cmdList, D3D12NrGuideKind kind,
                    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept;
    void ReleaseAfterIdle() noexcept;

    ID3D12Resource* Get(D3D12NrGuideKind kind) const noexcept;
    D3D12_RESOURCE_STATES State(D3D12NrGuideKind kind) const noexcept;

private:
    struct Clone {
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_DESC desc{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    };

    static bool SameDesc(const D3D12_RESOURCE_DESC& a,
                         const D3D12_RESOURCE_DESC& b) noexcept;
    static ID3D12Resource* Create(ID3D12Device* device,
                                  const D3D12_RESOURCE_DESC& desc) noexcept;
    static bool Park(Clone& clone, NrDeferredRetirementQueue& retirement) noexcept;
    static void Release(Clone& clone) noexcept;

    Clone* Slot(D3D12NrGuideKind kind) noexcept;
    const Clone* Slot(D3D12NrGuideKind kind) const noexcept;

    Clone depth_{};
    Clone motion_{};
};

} // namespace nrfusion
