#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>

#include "nrfusion/D3D12NrScratchAccounting.hpp"
#include "nrfusion/NrDeferredRetirementQueue.hpp"

namespace nrfusion {

enum class D3D12NrScratchKind : std::uint8_t {
    Output,
    ColorCopy,
    HdrCopy,
    PassScratch,
    ColorSmall,
    OutputNative,
    ActiveColor,
    ResidualEdited,
    ResidualHistory0,
    ResidualHistory1,
    ResidualComposed
};

inline const char* KindToString(D3D12NrScratchKind kind) noexcept {
    switch (kind) {
    case D3D12NrScratchKind::Output: return "Output";
    case D3D12NrScratchKind::ColorCopy: return "ColorCopy";
    case D3D12NrScratchKind::HdrCopy: return "HdrCopy";
    case D3D12NrScratchKind::PassScratch: return "PassScratch";
    case D3D12NrScratchKind::ColorSmall: return "ColorSmall";
    case D3D12NrScratchKind::OutputNative: return "OutputNative";
    case D3D12NrScratchKind::ActiveColor: return "ActiveColor";
    case D3D12NrScratchKind::ResidualEdited: return "ResidualEdited";
    case D3D12NrScratchKind::ResidualHistory0: return "ResidualHistory0";
    case D3D12NrScratchKind::ResidualHistory1: return "ResidualHistory1";
    case D3D12NrScratchKind::ResidualComposed: return "ResidualComposed";
    }
    return "Unknown";
}

struct D3D12NrScratchUsage {
    bool passScratch = false;
    bool colorSmall = false;
    bool outputNative = false;
    bool activeColor = false;
    bool residual = false;
    bool residualComposed = false;
};

struct D3D12NrScratchDesc {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t frameWidth = 0;
    std::uint32_t frameHeight = 0;
    std::uint32_t workWidth = 0;
    std::uint32_t workHeight = 0;
    bool needsHdrCopy = true;

    bool operator==(const D3D12NrScratchDesc&) const noexcept = default;
};

class D3D12NrScratchResources {
public:
    D3D12NrScratchResources() = default;
    D3D12NrScratchResources(const D3D12NrScratchResources&) = delete;
    D3D12NrScratchResources& operator=(const D3D12NrScratchResources&) = delete;
    D3D12NrScratchResources(D3D12NrScratchResources&&) = delete;
    D3D12NrScratchResources& operator=(D3D12NrScratchResources&&) = delete;

    bool Ensure(ID3D12Device* device, const D3D12NrScratchDesc& desc,
                NrDeferredRetirementQueue& retirement,
                ID3D12Fence* fence = nullptr,
                std::uint64_t fenceValue = 0) noexcept;
    bool EnsureOptional(ID3D12Device* device, D3D12NrScratchKind kind,
                        DXGI_FORMAT format, std::uint32_t width, std::uint32_t height,
                        NrDeferredRetirementQueue& retirement,
                        ID3D12Fence* fence = nullptr,
                        std::uint64_t fenceValue = 0) noexcept;
    bool RetireUnused(const D3D12NrScratchUsage& usage,
                      NrDeferredRetirementQueue& retirement,
                      ID3D12Fence* fence = nullptr,
                      std::uint64_t fenceValue = 0) noexcept;
    bool Retire(D3D12NrScratchKind kind, NrDeferredRetirementQueue& retirement,
                ID3D12Fence* fence = nullptr,
                std::uint64_t fenceValue = 0) noexcept;
    bool Retire(NrDeferredRetirementQueue& retirement,
                ID3D12Fence* fence = nullptr,
                std::uint64_t fenceValue = 0) noexcept;
    bool QueueTransition(D3D12NrScratchKind kind, D3D12_RESOURCE_STATES expected,
                         D3D12_RESOURCE_STATES next, D3D12_RESOURCE_BARRIER& barrier) noexcept;
    bool Transition(ID3D12GraphicsCommandList* cmdList, D3D12NrScratchKind kind,
                    D3D12_RESOURCE_STATES expected, D3D12_RESOURCE_STATES next) noexcept;
    bool RestoreAllToUav(ID3D12GraphicsCommandList* cmdList) noexcept;
    void ReleaseAfterIdle() noexcept;

    bool Complete() const noexcept;
    bool Matches(const D3D12NrScratchDesc& desc) const noexcept;
    ID3D12Resource* Get(D3D12NrScratchKind kind) const noexcept;
    D3D12_RESOURCE_STATES State(D3D12NrScratchKind kind) const noexcept;
    D3D12NrScratchAccounting Accounting(const NrDeferredRetirementQueue* retirement = nullptr) const noexcept;
    std::uint64_t PhysicalBytes(D3D12NrScratchKind kind) const noexcept;

private:
    struct Surface {
        ID3D12Resource* resource = nullptr;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        std::uint64_t physicalBytes = 0;
    };

    static ID3D12Resource* Create(ID3D12Device* device, DXGI_FORMAT format,
                                  std::uint32_t width, std::uint32_t height,
                                  std::uint64_t* outPhysicalBytes = nullptr) noexcept;
    static Surface MakeSurface(ID3D12Resource* resource, DXGI_FORMAT format,
                               std::uint32_t width, std::uint32_t height,
                               std::uint64_t physicalBytes = 0) noexcept;
    static void Release(Surface& surface) noexcept;
    static std::uint64_t LogicalBytes(const Surface& surface) noexcept;
    static std::uint64_t PhysicalBytes(const Surface& surface) noexcept;
    static bool Park(Surface& surface, NrDeferredRetirementQueue& retirement,
                     const char* kindName = nullptr,
                     ID3D12Fence* fence = nullptr,
                     std::uint64_t fenceValue = 0) noexcept;
    static bool IsCoreKind(D3D12NrScratchKind kind) noexcept;

    Surface* Slot(D3D12NrScratchKind kind) noexcept;
    const Surface* Slot(D3D12NrScratchKind kind) const noexcept;
    std::size_t ActiveCount() const noexcept;
    bool ParkAll(NrDeferredRetirementQueue& retirement,
                 ID3D12Fence* fence = nullptr,
                 std::uint64_t fenceValue = 0) noexcept;

    Surface output_{};
    Surface colorCopy_{};
    Surface hdrCopy_{};
    Surface passScratch_{};
    Surface colorSmall_{};
    Surface outputNative_{};
    Surface activeColor_{};
    Surface residualEdited_{};
    Surface residualHistory0_{};
    Surface residualHistory1_{};
    Surface residualComposed_{};
    D3D12NrScratchDesc desc_{};
    mutable std::uint64_t peakLogicalBytes_ = 0;
    mutable std::uint64_t peakPhysicalBytes_ = 0;
};

} // namespace nrfusion
