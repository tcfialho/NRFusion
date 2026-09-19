#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace nrfusion {

struct D3D12AsyncFenceToken {
    std::uint64_t workId = 0;
    std::uint64_t producerValue = 0;
    std::uint64_t completionValue = 0;
};

// Queue-to-queue synchronization for an async D3D12 executor. This deliberately uses only the
// ID3D12CommandQueue/ID3D12Fence method surface through templates, so the production sequencing is
// compiled by the portable build and instantiates directly with COM interfaces on Windows.
// No CPU wait is ever issued here.
class D3D12AsyncFenceSequencer {
public:
    // Work/session resets must NOT rewind fence values. D3D12 fences may outlive a neural
    // session; waiting on a reused low value would complete immediately if the fence had already
    // reached a higher value. Keep sequence values process-monotonic. New fence objects may safely
    // start at any higher signal value.
    void Reset() noexcept {}

    template <typename QueueT, typename FenceT>
    std::optional<D3D12AsyncFenceToken> QueueComputeAfterProducer(
        std::uint64_t workId, QueueT* producerQueue, QueueT* computeQueue, FenceT* producerFence) {
        if (workId == 0 || producerQueue == nullptr || computeQueue == nullptr || producerFence == nullptr)
            return std::nullopt;

        if (producerValue_ == (std::numeric_limits<std::uint64_t>::max)()) return std::nullopt;
        const std::uint64_t producerValue = ++producerValue_;
        if (!Succeeded(producerQueue->Signal(producerFence, producerValue))) return std::nullopt;
        if (!Succeeded(computeQueue->Wait(producerFence, producerValue))) return std::nullopt;
        return D3D12AsyncFenceToken{workId, producerValue, 0};
    }

    template <typename QueueT, typename FenceT>
    bool SignalComputeComplete(D3D12AsyncFenceToken& token, QueueT* computeQueue,
                               FenceT* completionFence) {
        if (token.workId == 0 || token.producerValue == 0 || token.completionValue != 0 ||
            computeQueue == nullptr || completionFence == nullptr)
            return false;
        if (completionValue_ == (std::numeric_limits<std::uint64_t>::max)()) return false;
        const std::uint64_t completionValue = ++completionValue_;
        if (!Succeeded(computeQueue->Signal(completionFence, completionValue))) return false;
        token.completionValue = completionValue;
        return true;
    }

    template <typename QueueT, typename FenceT>
    bool QueueConsumerAfterCompute(const D3D12AsyncFenceToken& token, QueueT* consumerQueue,
                                   FenceT* completionFence) const {
        if (token.workId == 0 || token.completionValue == 0 ||
            consumerQueue == nullptr || completionFence == nullptr)
            return false;
        return Succeeded(consumerQueue->Wait(completionFence, token.completionValue));
    }

    std::uint64_t ProducerValue() const noexcept { return producerValue_; }
    std::uint64_t CompletionValue() const noexcept { return completionValue_; }

private:
    template <typename HrT>
    static bool Succeeded(HrT hr) noexcept {
        return static_cast<long long>(hr) >= 0;
    }

    std::uint64_t producerValue_ = 0;
    std::uint64_t completionValue_ = 0;
};

} // namespace nrfusion
