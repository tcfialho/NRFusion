#pragma once

#include "nrfusion/WorkLedger.hpp"
#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace nrfusion {

struct TimingMapEntry {
    bool mapsWork = false;
    WorkTicket ticket{};
};

// Bounded FIFO matching serial GPU timestamp intervals to exact WorkIds. PushInvalid() represents
// an interval that was opened but whose NR attempt failed/aborted; consuming it must not complete
// the next valid workload by mistake.
class TimingWorkMapper {
public:
    explicit TimingWorkMapper(std::size_t capacity = 16) : capacity_(capacity ? capacity : 1) {}

    std::optional<WorkTicket> Push(const WorkTicket& ticket);
    std::optional<WorkTicket> PushInvalid();
    std::optional<TimingMapEntry> Pop();
    void Reset() { queue_.clear(); }
    std::size_t Size() const noexcept { return queue_.size(); }
    std::size_t Capacity() const noexcept { return capacity_; }

private:
    std::optional<WorkTicket> PushEntry(TimingMapEntry entry);
    std::size_t capacity_;
    std::deque<TimingMapEntry> queue_;
};

// Slot-addressable mapping for APIs whose timestamp rings expose the exact query slot (Vulkan).
// Assignment reports acceptance separately from displacement: an empty valid slot and an invalid
// slot are not the same outcome.
struct TimingSlotAssignResult {
    bool accepted = false;
    std::optional<WorkTicket> displaced;
};

class TimingSlotMapper {
public:
    explicit TimingSlotMapper(std::size_t capacity = 8)
        : slots_(capacity ? capacity : 1) {}

    TimingSlotAssignResult Assign(std::size_t slot, const WorkTicket& ticket);
    std::optional<WorkTicket> Take(std::size_t slot);
    std::optional<WorkTicket> Clear(std::size_t slot);
    void Reset();
    std::size_t Capacity() const noexcept { return slots_.size(); }

private:
    std::vector<std::optional<WorkTicket>> slots_;
};

} // namespace nrfusion
