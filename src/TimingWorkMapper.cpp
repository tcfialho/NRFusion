#include "nrfusion/TimingWorkMapper.hpp"

namespace nrfusion {

std::optional<WorkTicket> TimingWorkMapper::PushEntry(TimingMapEntry entry) {
    std::optional<WorkTicket> dropped;
    if (queue_.size() >= capacity_) {
        if (queue_.front().mapsWork) dropped = queue_.front().ticket;
        queue_.pop_front();
    }
    queue_.push_back(entry);
    return dropped;
}

std::optional<WorkTicket> TimingWorkMapper::Push(const WorkTicket& ticket) {
    return PushEntry(TimingMapEntry{true, ticket});
}

std::optional<WorkTicket> TimingWorkMapper::PushInvalid() {
    return PushEntry(TimingMapEntry{});
}

std::optional<TimingMapEntry> TimingWorkMapper::Pop() {
    if (queue_.empty()) return std::nullopt;
    auto out = queue_.front();
    queue_.pop_front();
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
