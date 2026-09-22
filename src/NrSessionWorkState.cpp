#include "nrfusion/NrSessionWorkState.hpp"

#include <cmath>
#include <limits>

namespace nrfusion {

NrSessionWorkTracker::Entry* NrSessionWorkTracker::Find(
    const WorkTicket& ticket) noexcept {
    if (ticket.id == 0 || ticket.session != session_) return nullptr;
    for (Entry& entry : entries_)
        if (entry.occupied && entry.ticket == ticket) return &entry;
    return nullptr;
}

const NrSessionWorkTracker::Entry* NrSessionWorkTracker::Find(
    const WorkTicket& ticket) const noexcept {
    if (ticket.id == 0 || ticket.session != session_) return nullptr;
    for (const Entry& entry : entries_)
        if (entry.occupied && entry.ticket == ticket) return &entry;
    return nullptr;
}

NrSessionWorkTracker::Entry* NrSessionWorkTracker::FindFree() noexcept {
    for (Entry& entry : entries_)
        if (!entry.occupied) return &entry;
    return nullptr;
}

std::optional<WorkTicket> NrSessionWorkTracker::Begin(
    std::uint64_t sourceFrame, std::uint64_t viewKey,
    std::uint64_t configurationGeneration, float workingScale,
    std::uint8_t precisionTag) noexcept {
    if (sourceFrame == 0 || !std::isfinite(workingScale) || workingScale <= 0.0f ||
        nextId_ == 0)
        return std::nullopt;
    Entry* entry = FindFree();
    if (entry == nullptr) return std::nullopt;

    const std::uint64_t id = nextId_;
    nextId_ = nextId_ == (std::numeric_limits<std::uint64_t>::max)()
        ? 0 : nextId_ + 1;
    WorkTicket ticket{
        id, session_, sourceFrame, viewKey, configurationGeneration,
        workingScale, precisionTag};
    *entry = {ticket, WorkState::Started, true};
    ++size_;
    return ticket;
}

bool NrSessionWorkTracker::Submit(const WorkTicket& ticket) noexcept {
    Entry* entry = Find(ticket);
    if (entry == nullptr || entry->state != WorkState::Started) return false;
    entry->state = WorkState::Submitted;
    return true;
}

bool NrSessionWorkTracker::Complete(const WorkTicket& ticket) noexcept {
    Entry* entry = Find(ticket);
    if (entry == nullptr || entry->state != WorkState::Submitted) return false;
    *entry = {};
    --size_;
    return true;
}

bool NrSessionWorkTracker::Abandon(const WorkTicket& ticket) noexcept {
    Entry* entry = Find(ticket);
    if (entry == nullptr) return false;
    *entry = {};
    --size_;
    return true;
}

bool NrSessionWorkTracker::IsSubmitted(const WorkTicket& ticket) const noexcept {
    const Entry* entry = Find(ticket);
    return entry != nullptr && entry->state == WorkState::Submitted;
}

void NrSessionWorkTracker::ResetSession() noexcept {
    for (Entry& entry : entries_) entry = {};
    size_ = 0;
    if (session_ == (std::numeric_limits<std::uint64_t>::max)()) session_ = 0;
    else ++session_;
}

std::optional<WorkTicket> NrSessionTimingQueue::PushEntry(
    NrSessionTimingEntry entry) noexcept {
    std::optional<WorkTicket> displaced;
    if (size_ == kCapacity) {
        const NrSessionTimingEntry& old = entries_[head_];
        if (old.mapsWork) displaced = old.ticket;
        head_ = (head_ + 1) % kCapacity;
        --size_;
    }
    const std::size_t tail = (head_ + size_) % kCapacity;
    entries_[tail] = entry;
    ++size_;
    return displaced;
}

std::optional<WorkTicket> NrSessionTimingQueue::Push(
    const WorkTicket& ticket) noexcept {
    return PushEntry({true, ticket});
}

std::optional<WorkTicket> NrSessionTimingQueue::PushInvalid() noexcept {
    return PushEntry({});
}

std::optional<NrSessionTimingEntry> NrSessionTimingQueue::Pop() noexcept {
    if (size_ == 0) return std::nullopt;
    const NrSessionTimingEntry entry = entries_[head_];
    entries_[head_] = {};
    head_ = (head_ + 1) % kCapacity;
    --size_;
    return entry;
}

} // namespace nrfusion
