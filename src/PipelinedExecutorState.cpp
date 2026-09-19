#include "nrfusion/PipelinedExecutorState.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace nrfusion {
namespace {
void AdvanceGeneration(std::uint64_t& generation) {
    if (generation == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("pipeline generation namespace exhausted");
    ++generation;
}
} // namespace

PipelinedExecutorState::PipelinedExecutorState(std::size_t capacity)
    : slots_(std::max<std::size_t>(1, capacity)) {}

void PipelinedExecutorState::Reset() {
    for (auto& slot : slots_) slot = {};
    AdvanceGeneration(generation_);
    reconfigurePending_ = false;
}

std::optional<PipelineTicket> PipelinedExecutorState::TrySubmit(std::uint64_t workId,
                                                                std::uint64_t frameId,
                                                                std::uint64_t viewKey) {
    if (reconfigurePending_ || workId == 0 || frameId == 0) return std::nullopt;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].state != SlotState::Free) continue;
        slots_[i] = {SlotState::InFlight, workId, frameId, viewKey};
        return PipelineTicket{generation_, workId, frameId, viewKey, i};
    }
    return std::nullopt;
}

bool PipelinedExecutorState::Complete(const PipelineTicket& ticket) {
    if (ticket.generation != generation_ || ticket.slot >= slots_.size()) return false;
    Slot& slot = slots_[ticket.slot];
    if (slot.state != SlotState::InFlight || slot.workId != ticket.workId ||
        slot.frameId != ticket.frameId || slot.viewKey != ticket.viewKey) return false;
    slot.state = SlotState::Ready;
    return true;
}

bool PipelinedExecutorState::Abandon(const PipelineTicket& ticket) {
    if (ticket.generation != generation_ || ticket.slot >= slots_.size()) return false;
    Slot& slot = slots_[ticket.slot];
    if (slot.state == SlotState::Free || slot.workId != ticket.workId ||
        slot.frameId != ticket.frameId || slot.viewKey != ticket.viewKey) return false;
    slot = {};
    return true;
}

std::optional<PipelineReadyFrame> PipelinedExecutorState::ConsumeLatestBefore(std::uint64_t currentFrameId,
                                                                               std::uint64_t viewKey) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const Slot& slot = slots_[i];
        if (slot.state != SlotState::Ready || slot.viewKey != viewKey || slot.frameId >= currentFrameId) continue;
        if (!best || slot.frameId > slots_[*best].frameId ||
            (slot.frameId == slots_[*best].frameId && slot.workId > slots_[*best].workId)) best = i;
    }
    if (!best) return std::nullopt;

    const Slot chosen = slots_[*best];
    PipelineReadyFrame ready{chosen.workId, chosen.frameId, chosen.viewKey, *best};
    // Any older/equal ready result for the same view is obsolete after consuming the newest one.
    for (auto& slot : slots_) {
        if (slot.state == SlotState::Ready && slot.viewKey == viewKey &&
            (slot.frameId < chosen.frameId ||
             (slot.frameId == chosen.frameId && slot.workId <= chosen.workId))) slot = {};
    }
    return ready;
}

std::size_t PipelinedExecutorState::DiscardReadyForReconfigure() {
    std::size_t discarded = 0;
    for (auto& slot : slots_) {
        if (slot.state == SlotState::Ready) { slot = {}; ++discarded; }
    }
    return discarded;
}

bool PipelinedExecutorState::ApplyReconfigureIfIdle() {
    if (!reconfigurePending_ || Outstanding() != 0) return false;
    AdvanceGeneration(generation_);
    reconfigurePending_ = false;
    return true;
}

std::size_t PipelinedExecutorState::Outstanding() const noexcept {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& s) {
        return s.state != SlotState::Free;
    }));
}

double PipelinedExecutorState::QueuePressure() const noexcept {
    return static_cast<double>(Outstanding()) / static_cast<double>(slots_.size());
}

} // namespace nrfusion
