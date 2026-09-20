#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace nrfusion {

enum class WorkState : std::uint8_t { Started, Submitted };

struct WorkTicket {
    std::uint64_t id = 0;
    std::uint64_t session = 0;
    std::uint64_t sourceFrame = 0;
    std::uint64_t viewKey = 0;
    std::uint64_t configurationGeneration = 0;
    float workingScale = 1.0f;
    std::uint8_t precisionTag = 0;
    bool operator==(const WorkTicket&) const noexcept = default;
};

// Identity/lifetime tracker for neural workloads. Work IDs are runtime-instance-unique and are never reused
// on ResetSession(), so a late completion from an old device/session cannot collide with new work.
class WorkLedger {
public:
    WorkTicket Begin(std::uint64_t sourceFrame, std::uint64_t viewKey = 0,
                     std::uint64_t configurationGeneration = 0, float workingScale = 1.0f,
                     std::uint8_t precisionTag = 0);
    bool Submit(const WorkTicket& ticket);
    bool Complete(const WorkTicket& ticket);
    bool Abandon(const WorkTicket& ticket);
    bool IsSubmitted(const WorkTicket& ticket) const noexcept;
    void ResetSession();

    std::size_t Outstanding() const noexcept { return entries_.size(); }
    std::uint64_t Session() const noexcept { return session_; }
    bool Contains(std::uint64_t workId) const noexcept;

private:
    struct Entry {
        WorkTicket ticket{};
        WorkState state = WorkState::Started;
    };

    std::uint64_t nextId_ = 1;
    std::uint64_t session_ = 1;
    std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace nrfusion
