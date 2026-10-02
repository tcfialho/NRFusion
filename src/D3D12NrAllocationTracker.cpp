#include "nrfusion/D3D12NrAllocationTracker.hpp"
#include <algorithm>
#include <cstdio>

namespace nrfusion {

D3D12NrAllocationTracker& D3D12NrAllocationTracker::Instance() noexcept {
    static D3D12NrAllocationTracker instance;
    return instance;
}

void D3D12NrAllocationTracker::RecordAllocation(const NrAllocationRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : records_) {
        if (r.owner == record.owner && r.kind == record.kind) {
            r = record;
            return;
        }
    }
    records_.push_back(record);
}

void D3D12NrAllocationTracker::RecordAllocation(
    std::string owner, std::string kind,
    DXGI_FORMAT format, std::uint32_t width,
    std::uint32_t height, std::uint64_t logicalBytes,
    std::uint64_t physicalBytes, bool persistent,
    bool active) {
    NrAllocationRecord rec;
    rec.owner = std::move(owner);
    rec.kind = std::move(kind);
    rec.format = format;
    rec.width = width;
    rec.height = height;
    rec.logicalBytes = logicalBytes;
    rec.physicalBytes = physicalBytes;
    rec.persistent = persistent;
    rec.active = active;
    RecordAllocation(rec);
}

void D3D12NrAllocationTracker::RecordRetirement(const std::string& owner, const std::string& kind) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& r : records_) {
        if (r.owner == owner && r.kind == kind) {
            r.active = false;
            return;
        }
    }
}

void D3D12NrAllocationTracker::Clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

std::vector<NrAllocationRecord> D3D12NrAllocationTracker::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

std::uint64_t D3D12NrAllocationTracker::TotalLogicalBytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& r : records_) {
        if (r.active) total += r.logicalBytes;
    }
    return total;
}

std::uint64_t D3D12NrAllocationTracker::TotalPhysicalBytes() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& r : records_) {
        if (r.active) total += r.physicalBytes;
    }
    return total;
}

std::size_t D3D12NrAllocationTracker::ActiveAllocationCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& r : records_) {
        if (r.active) ++count;
    }
    return count;
}

void D3D12NrAllocationTracker::LogSummary() const {
    const auto snapshot = Snapshot();
    std::uint64_t activeLogical = 0;
    std::uint64_t activePhysical = 0;
    std::uint64_t retiredLogical = 0;
    std::uint64_t retiredPhysical = 0;

    std::printf("[VRAM Tracker] --- Resource Allocation Snapshot ---\n");
    for (const auto& r : snapshot) {
        std::printf("  [%s::%s] %ux%u (fmt=%d) | logical: %.2f MiB, physical: %.2f MiB | %s | %s\n",
                    r.owner.c_str(), r.kind.c_str(), r.width, r.height, static_cast<int>(r.format),
                    static_cast<double>(r.logicalBytes) / (1024.0 * 1024.0),
                    static_cast<double>(r.physicalBytes) / (1024.0 * 1024.0),
                    r.persistent ? "persistent" : "transient",
                    r.active ? "ACTIVE" : "RETIRED");
        if (r.active) {
            activeLogical += r.logicalBytes;
            activePhysical += r.physicalBytes;
        } else {
            retiredLogical += r.logicalBytes;
            retiredPhysical += r.physicalBytes;
        }
    }
    std::printf("[VRAM Tracker] Steady (Active) : logical=%.2f MiB, physical=%.2f MiB\n",
                static_cast<double>(activeLogical) / (1024.0 * 1024.0),
                static_cast<double>(activePhysical) / (1024.0 * 1024.0));
    std::printf("[VRAM Tracker] Peak (Act+Ret)  : logical=%.2f MiB, physical=%.2f MiB\n",
                static_cast<double>(activeLogical + retiredLogical) / (1024.0 * 1024.0),
                static_cast<double>(activePhysical + retiredPhysical) / (1024.0 * 1024.0));
}

} // namespace nrfusion
