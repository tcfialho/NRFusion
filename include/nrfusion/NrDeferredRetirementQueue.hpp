#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nrfusion {

enum class NrRetiredObjectKind : std::uint8_t {
    Feature,
    Resource
};

struct NrRetiredObject {
    void* object = nullptr;
    NrRetiredObjectKind kind = NrRetiredObjectKind::Feature;
};

class NrDeferredRetirementQueue {
public:
    static constexpr std::size_t kCapacity = 64;
    static constexpr std::uint32_t kDefaultDelay = 32;

    using ReleaseFn = void (*)(void* context, NrRetiredObject retired) noexcept;

    bool Park(void*& object, NrRetiredObjectKind kind,
              std::uint32_t delay = kDefaultDelay) noexcept;
    void Tick(void* context, ReleaseFn release) noexcept;
    void DrainAfterIdle(void* context, ReleaseFn release) noexcept;

    std::size_t Size() const noexcept { return size_; }

private:
    struct Entry {
        NrRetiredObject retired{};
        std::uint32_t framesLeft = 0;
        bool occupied = false;
    };

    std::array<Entry, kCapacity> entries_{};
    std::size_t size_ = 0;
};

} // namespace nrfusion
