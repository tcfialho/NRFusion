#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

struct ID3D12Fence;

namespace nrfusion {

enum class NrRetiredObjectKind : std::uint8_t {
    Feature,
    Resource
};

struct NrRetiredObject {
    void* object = nullptr;
    NrRetiredObjectKind kind = NrRetiredObjectKind::Feature;
    std::uint64_t logicalBytes = 0;
    std::uint64_t physicalBytes = 0;
};

struct NrDeferredRetirementAccounting {
    std::size_t resourceCount = 0;
    std::uint64_t logicalBytes = 0;
    std::uint64_t physicalBytes = 0;
    bool logicalBytesExact = true;
    bool physicalBytesExact = true;
};

class NrDeferredRetirementQueue {
public:
    static constexpr std::size_t kCapacity = 64;
    static constexpr std::uint32_t kDefaultDelay = 32;

    using ReleaseFn = void (*)(void* context, NrRetiredObject retired) noexcept;

    ~NrDeferredRetirementQueue() noexcept;

    bool Park(void*& object, NrRetiredObjectKind kind,
              std::uint32_t delay = kDefaultDelay,
              std::uint64_t logicalBytes = 0,
              std::uint64_t physicalBytes = 0,
              ID3D12Fence* completionFence = nullptr,
              std::uint64_t completionValue = 0) noexcept;
    void Tick(void* context, ReleaseFn release) noexcept;
    void DrainAfterIdle(void* context, ReleaseFn release) noexcept;

    std::size_t Size() const noexcept { return size_; }
    NrDeferredRetirementAccounting ResourceAccounting() const noexcept;

private:
    struct Entry {
        NrRetiredObject retired{};
        std::uint32_t framesLeft = 0;
        ID3D12Fence* fence = nullptr;
        std::uint64_t fenceValue = 0;
        bool occupied = false;
    };

    std::array<Entry, kCapacity> entries_{};
    std::size_t size_ = 0;
};

} // namespace nrfusion
