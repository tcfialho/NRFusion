#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace nrfusion {

enum class NgxFeatureKind : std::uint8_t {
    Unknown,
    SuperResolution,
    RayReconstruction,
    FrameGeneration
};

enum class NgxEvaluateAction : std::uint8_t {
    PassThrough,
    NeuralRendering
};

struct NgxFeatureToken {
    std::uintptr_t handle = 0;
    std::uint64_t contextId = 0;
    std::uint64_t generation = 0;

    constexpr explicit operator bool() const noexcept {
        return handle != 0 && contextId != 0 && generation != 0;
    }

    constexpr bool operator==(const NgxFeatureToken&) const noexcept = default;
};

struct NgxFeatureIdentity {
    NgxFeatureToken token{};
    NgxFeatureKind kind = NgxFeatureKind::Unknown;

    constexpr explicit operator bool() const noexcept {
        return static_cast<bool>(token);
    }
};

NgxFeatureKind ClassifyNgxFeatureId(std::int32_t featureId) noexcept;

struct NgxFeatureCreateEvent {
    std::uintptr_t handle = 0;
    std::uint64_t contextId = 0;
    std::int32_t featureId = 0;
    bool succeeded = false;
};

class NgxFeatureRegistry {
public:
    static constexpr std::size_t kCapacity = 64;

    NgxFeatureIdentity RecordCreate(NgxFeatureCreateEvent event) noexcept;
    bool RecordRelease(NgxFeatureToken token) noexcept;

    NgxFeatureIdentity Lookup(std::uint64_t contextId, std::uintptr_t handle) const noexcept;
    NgxEvaluateAction ActionFor(std::uint64_t contextId, std::uintptr_t handle) const noexcept;

    void Clear() noexcept;
    std::size_t Size() const noexcept { return size_; }

private:
    struct Slot {
        NgxFeatureIdentity identity{};
        bool occupied = false;
    };

    std::size_t Find(std::uint64_t contextId, std::uintptr_t handle) const noexcept;
    std::size_t FindFree() const noexcept;

    std::array<Slot, kCapacity> slots_{};
    std::size_t size_ = 0;
    std::uint64_t nextGeneration_ = 1;
};

} // namespace nrfusion
