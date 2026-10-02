#pragma once

#include "nrfusion/NrTimingSource.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace nrfusion::test {

class FakeTimingSource {
public:
    static constexpr std::size_t kMaxCapacity = 16;

    explicit FakeTimingSource(std::size_t capacity = 4) noexcept
        : capacity_(capacity == 0 ? 1 : capacity > kMaxCapacity ? kMaxCapacity : capacity) {}

    bool Push(const WorkTicket& ticket, double gpuMs, std::uint64_t completionValue) noexcept {
        if (ticket.id == 0 || ticket.session == 0)
            return false;
        return PushEntry({true, ticket, gpuMs}, completionValue);
    }

    bool PushInvalid(std::uint64_t completionValue) noexcept {
        return PushEntry({}, completionValue);
    }

    std::optional<NrRetiredTimingSample> TryRetire(
        std::uint64_t completedValue) noexcept {
        if (size_ == 0) return std::nullopt;
        Entry& entry = entries_[head_];
        if (entry.completionValue > completedValue) return std::nullopt;
        const NrRetiredTimingSample sample = entry.sample;
        entry = {};
        head_ = (head_ + 1) % capacity_;
        --size_;
        if (size_ == 0) head_ = 0;
        return sample;
    }

    void ResetAfterIdle() noexcept {
        for (auto& entry : entries_) entry = {};
        head_ = 0;
        size_ = 0;
        lastCompletionValue_ = 0;
    }

    std::size_t Size() const noexcept { return size_; }
    std::size_t Capacity() const noexcept { return capacity_; }

private:
    struct Entry {
        NrRetiredTimingSample sample{};
        std::uint64_t completionValue = 0;
    };

    bool PushEntry(NrRetiredTimingSample sample, std::uint64_t completionValue) noexcept {
        if (size_ == capacity_ || completionValue == 0 ||
            completionValue <= lastCompletionValue_)
            return false;
        const std::size_t tail = (head_ + size_) % capacity_;
        entries_[tail] = {sample, completionValue};
        ++size_;
        lastCompletionValue_ = completionValue;
        return true;
    }

    std::array<Entry, kMaxCapacity> entries_{};
    std::size_t capacity_ = 4;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t lastCompletionValue_ = 0;
};

static_assert(NrTimingSource<FakeTimingSource>);

}
