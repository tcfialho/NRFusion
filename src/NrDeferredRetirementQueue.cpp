#include "nrfusion/NrDeferredRetirementQueue.hpp"

#if defined(_WIN32)
#include <d3d12.h>
#else
struct ID3D12Fence {
    virtual unsigned long long GetCompletedValue() = 0;
    virtual unsigned long AddRef() = 0;
    virtual unsigned long Release() = 0;
    virtual ~ID3D12Fence() = default;
};
#endif

#include <limits>

namespace nrfusion {

NrDeferredRetirementQueue::~NrDeferredRetirementQueue() noexcept {
    for (auto& entry : entries_) {
        if (entry.occupied && entry.fence != nullptr) {
            entry.fence->Release();
            entry.fence = nullptr;
        }
    }
}

bool NrDeferredRetirementQueue::Park(
    void*& object, NrRetiredObjectKind kind,
    std::uint32_t delay, std::uint64_t logicalBytes,
    std::uint64_t physicalBytes,
    ID3D12Fence* completionFence,
    std::uint64_t completionValue) noexcept {
    if (object == nullptr || delay == 0) return false;
    if (kind != NrRetiredObjectKind::Feature && kind != NrRetiredObjectKind::Resource)
        return false;

    for (auto& entry : entries_) {
        if (entry.occupied) continue;
        entry.retired = {object, kind, logicalBytes, physicalBytes};
        entry.framesLeft = delay;
        entry.fence = completionFence;
        entry.fenceValue = completionValue;
        if (entry.fence != nullptr) {
            entry.fence->AddRef();
        }
        entry.occupied = true;
        object = nullptr;
        ++size_;
        return true;
    }
    return false;
}

NrDeferredRetirementAccounting
NrDeferredRetirementQueue::ResourceAccounting() const noexcept {
    NrDeferredRetirementAccounting result{};
    for (const auto& entry : entries_) {
        if (!entry.occupied ||
            entry.retired.kind != NrRetiredObjectKind::Resource)
            continue;
        ++result.resourceCount;
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        if (entry.retired.logicalBytes == 0) {
            result.logicalBytesExact = false;
        } else {
            if (result.logicalBytes > max - entry.retired.logicalBytes) {
                result.logicalBytes = max;
                result.logicalBytesExact = false;
            } else {
                result.logicalBytes += entry.retired.logicalBytes;
            }
        }
        if (entry.retired.physicalBytes == 0) {
            result.physicalBytesExact = false;
        } else {
            if (result.physicalBytes > max - entry.retired.physicalBytes) {
                result.physicalBytes = max;
                result.physicalBytesExact = false;
            } else {
                result.physicalBytes += entry.retired.physicalBytes;
            }
        }
    }
    return result;
}

void NrDeferredRetirementQueue::Tick(void* context, ReleaseFn release) noexcept {
    if (release == nullptr) return;

    for (auto& entry : entries_) {
        if (!entry.occupied) continue;

        bool readyToRelease = false;
        if (entry.fence != nullptr) {
            if (entry.fence->GetCompletedValue() >= entry.fenceValue) {
                readyToRelease = true;
            }
        }

        if (!readyToRelease) {
            if (entry.framesLeft > 1) {
                --entry.framesLeft;
                continue;
            }
            readyToRelease = true;
        }

        if (readyToRelease) {
            if (entry.fence != nullptr) {
                entry.fence->Release();
                entry.fence = nullptr;
            }
            release(context, entry.retired);
            entry = {};
            --size_;
        }
    }
}

void NrDeferredRetirementQueue::DrainAfterIdle(void* context, ReleaseFn release) noexcept {
    if (release == nullptr) return;

    for (auto& entry : entries_) {
        if (!entry.occupied) continue;
        if (entry.fence != nullptr) {
            entry.fence->Release();
            entry.fence = nullptr;
        }
        release(context, entry.retired);
        entry = {};
    }
    size_ = 0;
}

} // namespace nrfusion
