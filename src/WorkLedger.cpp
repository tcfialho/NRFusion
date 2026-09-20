#include "nrfusion/WorkLedger.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace nrfusion {

WorkLedger::WorkLedger() {
    entries_.reserve(kTypicalOutstanding);
}

std::size_t WorkLedger::FindIndex(std::uint64_t workId) const noexcept {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].ticket.id == workId) return i;
    return entries_.size();
}

WorkTicket WorkLedger::Begin(std::uint64_t sourceFrame, std::uint64_t viewKey,
                             std::uint64_t configurationGeneration, float workingScale,
                             std::uint8_t precisionTag) {
    if (sourceFrame == 0 || !std::isfinite(workingScale) || workingScale <= 0.0f)
        throw std::invalid_argument("NRFusion work identity requires a valid source frame and scale");
    // ID zero is reserved as invalid. Work IDs are runtime-instance-unique; once the 64-bit namespace is
    // exhausted, fail closed instead of reusing an ID that a very late completion could still carry.
    if (nextId_ == 0) throw std::overflow_error("NRFusion WorkId space exhausted");
    const std::uint64_t id = nextId_;
    if (nextId_ == std::numeric_limits<std::uint64_t>::max()) nextId_ = 0;
    else ++nextId_;
    WorkTicket ticket{id, session_, sourceFrame, viewKey, configurationGeneration, workingScale, precisionTag};
    entries_.push_back(Entry{ticket, WorkState::Started});
    return ticket;
}

bool WorkLedger::Submit(const WorkTicket& ticket) {
    if (ticket.session != session_ || ticket.id == 0) return false;
    const std::size_t index = FindIndex(ticket.id);
    if (index == entries_.size() || !(entries_[index].ticket == ticket) ||
        entries_[index].state != WorkState::Started) return false;
    entries_[index].state = WorkState::Submitted;
    return true;
}

bool WorkLedger::Complete(const WorkTicket& ticket) {
    if (ticket.session != session_ || ticket.id == 0) return false;
    const std::size_t index = FindIndex(ticket.id);
    if (index == entries_.size() || !(entries_[index].ticket == ticket) ||
        entries_[index].state != WorkState::Submitted) return false;
    if (index + 1 != entries_.size()) entries_[index] = entries_.back();
    entries_.pop_back();
    return true;
}

bool WorkLedger::Abandon(const WorkTicket& ticket) {
    if (ticket.session != session_ || ticket.id == 0) return false;
    const std::size_t index = FindIndex(ticket.id);
    if (index == entries_.size() || !(entries_[index].ticket == ticket)) return false;
    if (index + 1 != entries_.size()) entries_[index] = entries_.back();
    entries_.pop_back();
    return true;
}

bool WorkLedger::IsSubmitted(const WorkTicket& ticket) const noexcept {
    if (ticket.session != session_ || ticket.id == 0) return false;
    const std::size_t index = FindIndex(ticket.id);
    return index != entries_.size() && entries_[index].state == WorkState::Submitted &&
           entries_[index].ticket == ticket;
}

void WorkLedger::ResetSession() {
    if (session_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("NRFusion session namespace exhausted");
    entries_.clear();
    ++session_;
    // nextId_ deliberately does not reset.
}

bool WorkLedger::Contains(std::uint64_t workId) const noexcept {
    return FindIndex(workId) != entries_.size();
}

} // namespace nrfusion
