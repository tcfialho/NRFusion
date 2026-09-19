#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace nrfusion {

struct PipelineTicket {
    std::uint64_t generation = 0;
    std::uint64_t workId = 0;
    std::uint64_t frameId = 0;
    std::uint64_t viewKey = 0;
    std::size_t slot = 0;
};

struct PipelineReadyFrame {
    std::uint64_t workId = 0;
    std::uint64_t frameId = 0;
    std::uint64_t viewKey = 0;
    std::size_t slot = 0;
};

// Portable state machine for non-blocking async/x86/MGPU executors.
// It owns no GPU objects; the host associates slot indices with resources/fences.
class PipelinedExecutorState {
public:
    explicit PipelinedExecutorState(std::size_t capacity = 2);

    void Reset();
    std::optional<PipelineTicket> TrySubmit(std::uint64_t workId, std::uint64_t frameId,
                                            std::uint64_t viewKey = 0);
    bool Complete(const PipelineTicket& ticket);
    bool Abandon(const PipelineTicket& ticket);

    // Returns the newest completed workload for this view strictly older than currentFrameId.
    // Ready results from the same view that are no newer than the consumed one are freed too.
    std::optional<PipelineReadyFrame> ConsumeLatestBefore(std::uint64_t currentFrameId,
                                                           std::uint64_t viewKey = 0);

    void RequestReconfigure() noexcept { reconfigurePending_ = true; }
    std::size_t DiscardReadyForReconfigure();
    bool ApplyReconfigureIfIdle();
    bool ReconfigurePending() const noexcept { return reconfigurePending_; }

    std::size_t Capacity() const noexcept { return slots_.size(); }
    std::size_t Outstanding() const noexcept;
    double QueuePressure() const noexcept;
    std::uint64_t Generation() const noexcept { return generation_; }

private:
    enum class SlotState : std::uint8_t { Free, InFlight, Ready };
    struct Slot {
        SlotState state = SlotState::Free;
        std::uint64_t workId = 0;
        std::uint64_t frameId = 0;
        std::uint64_t viewKey = 0;
    };

    std::vector<Slot> slots_;
    std::uint64_t generation_ = 1;
    bool reconfigurePending_ = false;
};

} // namespace nrfusion
