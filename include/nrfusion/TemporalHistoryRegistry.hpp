#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace nrfusion {

struct ViewDescriptor {
    std::uint64_t featureKey = 0; // stable identity of the game's temporal feature
    std::uint64_t viewKey = 0;    // optional stable sub-view identity (split-screen/VR eye/etc.)
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
};

struct HistoryLease {
    std::uint64_t historyId = 0;
    bool resetRequired = true;
};

// Assigns an independent temporal-history identity to each game feature/view and requests a reset
// when that view changes shape. GPU resources remain owned by the host; historyId is the host key.
class TemporalHistoryRegistry {
public:
    HistoryLease Acquire(const ViewDescriptor& view, std::uint64_t frameNumber);
    void InvalidateFeature(std::uint64_t featureKey);
    void Prune(std::uint64_t frameNumber, std::uint64_t maxIdleFrames = 600);
    void Clear();
    std::size_t Size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        ViewDescriptor descriptor{};
        std::uint64_t historyId = 0;
        std::uint64_t lastSeenFrame = 0;
    };

    struct RegistryKey {
        std::uint64_t feature = 0;
        std::uint64_t view = 0;
        bool operator==(const RegistryKey&) const = default;
    };
    struct RegistryKeyHash {
        std::size_t operator()(const RegistryKey& key) const noexcept;
    };

    static bool SameShape(const ViewDescriptor& a, const ViewDescriptor& b);
    std::uint64_t AllocateHistoryId();
    std::unordered_map<RegistryKey, Entry, RegistryKeyHash> entries_;
    std::uint64_t nextHistoryId_ = 1;
};

} // namespace nrfusion
