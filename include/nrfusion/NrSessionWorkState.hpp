#pragma once

#include "nrfusion/WorkLedger.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace nrfusion {

struct NrSessionTimingEntry {
    bool mapsWork = false;
    WorkTicket ticket{};
};

class NrSessionWorkTracker {
public:
    static constexpr std::size_t kCapacity = 64;

    std::optional<WorkTicket> Begin(
        std::uint64_t sourceFrame, std::uint64_t viewKey,
        std::uint64_t configurationGeneration, float workingScale,
        std::uint8_t precisionTag) noexcept;
    bool Submit(const WorkTicket& ticket) noexcept;
    bool Complete(const WorkTicket& ticket) noexcept;
    bool Abandon(const WorkTicket& ticket) noexcept;
    bool IsSubmitted(const WorkTicket& ticket) const noexcept;
    void ResetSession() noexcept;

    std::size_t Outstanding() const noexcept { return size_; }
    std::uint64_t Session() const noexcept { return session_; }

private:
    struct Entry {
        WorkTicket ticket{};
        WorkState state = WorkState::Started;
        bool occupied = false;
    };

    Entry* Find(const WorkTicket& ticket) noexcept;
    const Entry* Find(const WorkTicket& ticket) const noexcept;
    Entry* FindFree() noexcept;

    std::array<Entry, kCapacity> entries_{};
    std::uint64_t nextId_ = 1;
    std::uint64_t session_ = 1;
    std::size_t size_ = 0;
};

class NrSessionTimingQueue {
public:
    static constexpr std::size_t kCapacity = 16;

    std::optional<WorkTicket> Push(const WorkTicket& ticket) noexcept;
    std::optional<WorkTicket> PushInvalid() noexcept;
    std::optional<NrSessionTimingEntry> Pop() noexcept;
    void Reset() noexcept { head_ = 0; size_ = 0; }

    std::size_t Size() const noexcept { return size_; }

private:
    std::optional<WorkTicket> PushEntry(NrSessionTimingEntry entry) noexcept;

    std::array<NrSessionTimingEntry, kCapacity> entries_{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

} // namespace nrfusion
