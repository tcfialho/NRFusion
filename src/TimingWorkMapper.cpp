#include "nrfusion/TimingWorkMapper.hpp"

namespace nrfusion {

std::optional<WorkTicket> TimingWorkMapper::PushEntry(TimingMapEntry entry) {
    std::optional<WorkTicket> dropped;
    const std::size_t capacity = entries_.size();

    if (size_ == capacity) {
        if (entries_[head_].mapsWork) dropped = entries_[head_].ticket;
        entries_[head_] = entry;
        head_ = head_ + 1 == capacity ? 0 : head_ + 1;
        return dropped;
    }

    std::size_t tail = head_ + size_;
    if (tail >= capacity) tail -= capacity;
    entries_[tail] = entry;
    ++size_;
    return dropped;
}

std::optional<WorkTicket> TimingWorkMapper::Push(const WorkTicket& ticket) {
    return PushEntry(TimingMapEntry{true, ticket});
}

std::optional<WorkTicket> TimingWorkMapper::PushInvalid() {
    return PushEntry(TimingMapEntry{});
}

std::optional<TimingMapEntry> TimingWorkMapper::Pop() {
    if (size_ == 0) return std::nullopt;
    const TimingMapEntry out = entries_[head_];
    head_ = head_ + 1 == entries_.size() ? 0 : head_ + 1;
    --size_;
    if (size_ == 0) head_ = 0;
    return out;
}


TimingSlotAssignResult TimingSlotMapper::Assign(std::size_t slot, const WorkTicket& ticket) {
    if (slot >= slots_.size()) return {};
    auto displaced = slots_[slot];
    slots_[slot] = ticket;
    return {true, std::move(displaced)};
}

std::optional<WorkTicket> TimingSlotMapper::Take(std::size_t slot) {
    if (slot >= slots_.size()) return std::nullopt;
    auto out = slots_[slot];
    slots_[slot].reset();
    return out;
}

std::optional<WorkTicket> TimingSlotMapper::Clear(std::size_t slot) {
    return Take(slot);
}

void TimingSlotMapper::Reset() {
    for (auto& slot : slots_) slot.reset();
}

} // namespace nrfusion
