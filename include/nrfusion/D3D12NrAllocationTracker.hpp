#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <dxgiformat.h>

namespace nrfusion {

struct NrAllocationRecord {
    std::string owner;
    std::string kind;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t logicalBytes = 0;
    std::uint64_t physicalBytes = 0;
    bool persistent = true;
    bool active = true;
};

class D3D12NrAllocationTracker {
public:
    static D3D12NrAllocationTracker& Instance() noexcept;

    void RecordAllocation(const NrAllocationRecord& record);
    void RecordAllocation(std::string owner, std::string kind,
                          DXGI_FORMAT format, std::uint32_t width,
                          std::uint32_t height, std::uint64_t logicalBytes,
                          std::uint64_t physicalBytes, bool persistent = true,
                          bool active = true);
    void RecordRetirement(const std::string& owner, const std::string& kind);
    void Clear() noexcept;

    std::vector<NrAllocationRecord> Snapshot() const;
    std::uint64_t TotalLogicalBytes() const noexcept;
    std::uint64_t TotalPhysicalBytes() const noexcept;
    std::size_t ActiveAllocationCount() const noexcept;
    void LogSummary() const;

private:
    D3D12NrAllocationTracker() = default;
    mutable std::mutex mutex_;
    std::vector<NrAllocationRecord> records_;
};

} // namespace nrfusion
