#include "nrfusion/HostServer64.hpp"

#include <utility>

namespace nrfusion {

void HostServer64::CollectRetiredTransport() {
    if (!d3d12Fence_) return;
    const uint64_t completed = d3d12Fence_->GetCompletedValue();
    while (!retiredTransports_.empty() &&
           retiredTransports_.front().fenceValue <= completed) {
        retiredTransports_.pop_front();
    }
}

void HostServer64::RetireImportedTransport() {
    CollectRetiredTransport();

    const bool hasTransport = importedColor_ || importedResidual_ ||
        importedDepth_ || importedMotion_ ||
        importedProducerFence_ || importedConsumerFence_;
    if (!hasTransport) {
        importedTransportFenceValue_ = 0;
        return;
    }

    const bool alreadyRetired = importedTransportFenceValue_ == 0 ||
        !d3d12Fence_ ||
        d3d12Fence_->GetCompletedValue() >= importedTransportFenceValue_;
    if (alreadyRetired) {
        importedColor_.Reset();
        importedResidual_.Reset();
        importedDepth_.Reset();
        importedMotion_.Reset();
        importedProducerFence_.Reset();
        importedConsumerFence_.Reset();
        importedTransportFenceValue_ = 0;
        return;
    }

    RetiredTransport retired{};
    retired.color = std::move(importedColor_);
    retired.residual = std::move(importedResidual_);
    retired.depth = std::move(importedDepth_);
    retired.motion = std::move(importedMotion_);
    retired.producerFence = std::move(importedProducerFence_);
    retired.consumerFence = std::move(importedConsumerFence_);
    retired.fenceValue = importedTransportFenceValue_;
    retiredTransports_.push_back(std::move(retired));
    importedTransportFenceValue_ = 0;
}

} // namespace nrfusion
