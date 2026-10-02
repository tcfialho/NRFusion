#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>

#include "nrfusion/NrDeferredRetirementQueue.hpp"

namespace nrfusion {

enum class D3D12NrGuideKind : std::uint8_t {
    Depth,
    Motion
};

struct D3D12NrGuideCloneAccounting {
    std::size_t resourceCount = 0;
    std::uint64_t logicalBytes = 0;
    std::uint64_t physicalBytes = 0;
    std::uint64_t peakLogicalBytes = 0;
    std::uint64_t peakPhysicalBytes = 0;
    bool logicalBytesExact = true;
    bool physicalBytesExact = true;
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
                NrDeferredRetirementQueue& retirement,
                ID3D12Fence* fence = nullptr,
                std::uint64_t fenceValue = 0) noexcept;
    bool Retire(D3D12NrGuideKind kind, NrDeferredRetirementQueue& retirement,
                ID3D12Fence* fence = nullptr,
                std::uint64_t fenceValue = 0) noexcept;
    bool Retire(NrDeferredRetirementQueue& retirement,
                ID3D12Fence* fence = nullptr,
                std::uint64_t fenceValue = 0) noexcept;
    bool Transition(ID3D12GraphicsCommandList* cmdList, D3D12NrGuideKind kind,
                    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept;
    void ReleaseAfterIdle() noexcept;

    ID3D12Resource* Get(D3D12NrGuideKind kind) const noexcept;
    D3D12_RESOURCE_STATES State(D3D12NrGuideKind kind) const noexcept;
    D3D12NrGuideCloneAccounting Accounting() const noexcept;
    std::uint64_t PhysicalBytes(D3D12NrGuideKind kind) const noexcept;

private:
    struct Clone {
        ID3D12Resource* resource = nullptr;
        D3D12_RESOURCE_DESC desc{};
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
        std::uint64_t physicalBytes = 0;
    };

    static bool SameDesc(const D3D12_RESOURCE_DESC& a,
                         const D3D12_RESOURCE_DESC& b) noexcept;
    static ID3D12Resource* Create(ID3D12Device* device,
                                  const D3D12_RESOURCE_DESC& desc,
                                  std::uint64_t* outPhysicalBytes = nullptr) noexcept;
    static std::uint64_t LogicalBytes(const Clone& clone) noexcept;
    static bool Park(Clone& clone, NrDeferredRetirementQueue& retirement,
                     D3D12NrGuideKind kind,
                     ID3D12Fence* fence = nullptr,
                     std::uint64_t fenceValue = 0) noexcept;
    static void Release(Clone& clone) noexcept;

    Clone* Slot(D3D12NrGuideKind kind) noexcept;
    const Clone* Slot(D3D12NrGuideKind kind) const noexcept;

    Clone depth_{};
    Clone motion_{};
    mutable std::uint64_t peakLogicalBytes_ = 0;
    mutable std::uint64_t peakPhysicalBytes_ = 0;
};

} // namespace nrfusion
