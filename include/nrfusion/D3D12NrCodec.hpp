#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <dxgiformat.h>

namespace nrfusion {

enum class D3D12NrCodecMode : std::uint32_t {
    Encode = 0,
    Resolve = 1,
    Downsample = 2,
    Meter = 3,
    Calibrate = 4,
    EncodeResidual = 5,
    ApplyResidual = 6,
    UnitExposure = 7,
    NormalizeMotion = 8,
    ComposeMotion = 9,
    ApplyInterpolatedResidual = 10,
    ZeroMotion = 11
};

struct alignas(256) D3D12NrCodecConstants {
    std::uint32_t mode = 0;
    float whitePoint = 1.0f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float transferStrength = 1.0f;
    float colourStrength = 1.0f;
    std::uint32_t debugView = 0;
    float maxRatio = 4.0f;
    std::uint32_t passthrough = 0;
    float motionScaleX = 1.0f;
    float motionScaleY = 1.0f;
    std::uint32_t guideWidth = 0;
    std::uint32_t guideHeight = 0;
    std::uint32_t compareMode = 0;
    float compareSplit = 0.5f;
    float compareZoom = 1.0f;
    std::uint32_t compareSwap = 0;
    std::uint32_t transfer = 0;
    float debugScale = 1.0f;
    std::uint32_t reversibleMode = 0;
    std::uint32_t applyModel = 1;
    std::uint32_t useGameExposure = 0;
    float exposurePreMul = 1.0f;
    std::uint32_t skinProtection = 0;
    std::uint32_t showSkinMask = 0;
    float skinDetail = 1.0f;
    float skinColour = 1.0f;
    float environmentDetail = 1.0f;
    float environmentColour = 1.0f;
    float residualBlend = 0.0f;
    std::uint32_t residualHistoryValid = 0;
    std::uint32_t residualMotionBaseX = 0;
    std::uint32_t residualMotionBaseY = 0;
};
static_assert(sizeof(D3D12NrCodecConstants) == 256);

struct D3D12NrCodecResources {
    ID3D12Resource* source = nullptr;
    ID3D12Resource* model = nullptr;
    ID3D12Resource* original = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* previousEdit = nullptr;
    ID3D12Resource* target = nullptr;
    ID3D12Resource* keep = nullptr;
};

class D3D12NrCodec {
public:
    bool Init(ID3D12Device* device) noexcept;
    bool Dispatch(ID3D12GraphicsCommandList* commandList,
                  const D3D12NrCodecConstants& constants,
                  const D3D12NrCodecResources& resources) noexcept;
    void Shutdown() noexcept;

    bool Ready() const noexcept {
        return device_ != nullptr && rootSignature_ != nullptr && pipelineState_ != nullptr;
    }

private:
    static constexpr std::uint32_t kSrvCount = 5;
    static constexpr std::uint32_t kUavCount = 2;
    static constexpr std::uint32_t kCbvCount = 1;
    static constexpr std::uint32_t kDescriptorCount = kSrvCount + kUavCount + kCbvCount;
    static constexpr std::uint32_t kSlotCount = 48;

    struct Slot {
        ID3D12DescriptorHeap* heap = nullptr;
        ID3D12Resource* constants = nullptr;
    };

    bool CreateRootSignature() noexcept;
    bool CreatePipeline() noexcept;
    bool CreateSlots() noexcept;
    bool WriteSrv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept;
    bool WriteUav(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept;
    bool WriteConstants(Slot& slot, const D3D12NrCodecConstants& constants,
                        D3D12_CPU_DESCRIPTOR_HANDLE handle) noexcept;
    D3D12_CPU_DESCRIPTOR_HANDLE Handle(const Slot& slot, std::uint32_t index) const noexcept;
    static DXGI_FORMAT TypedFormat(DXGI_FORMAT format) noexcept;

    ID3D12Device* device_ = nullptr;
    ID3D12RootSignature* rootSignature_ = nullptr;
    ID3D12PipelineState* pipelineState_ = nullptr;
    std::array<Slot, kSlotCount> slots_{};
    std::uint32_t slotIndex_ = 0;
    std::uint32_t descriptorSize_ = 0;
};

} // namespace nrfusion
