#include "D3D12TestHarness.hpp"

namespace nrfusion::testing {

void D3D12TestHarness::SimulateNrPass(std::uint64_t frameIndex, SchedulerMode scheduler) {
    if (scheduler == SchedulerMode::AsyncCompute && computeQueue_ && computePso_) {
        // Execute real neural dispatch on compute queue
        computeAlloc_->Reset();
        computeCmdList_->Reset(computeAlloc_.Get(), computePso_.Get());

        computeCmdList_->EndQuery(timestampHeapCompute_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

        computeCmdList_->SetComputeRootSignature(computeRootSig_.Get());
        computeCmdList_->SetComputeRootUnorderedAccessView(0, computeScratchBuffer_->GetGPUVirtualAddress());
        computeCmdList_->Dispatch(64, 64, 1);

        computeCmdList_->EndQuery(timestampHeapCompute_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        computeCmdList_->ResolveQueryData(timestampHeapCompute_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                         timestampReadbackCompute_.Get(), 0);
        computeCmdList_->Close();

        // Queue-to-queue synchronization via D3D12AsyncFenceSequencer
        D3D12AsyncFenceSequencer sequencer;
        auto token = sequencer.QueueComputeAfterProducer(
            frameIndex, directQueue_.Get(), computeQueue_.Get(), directFence_.Get());

        ID3D12CommandList* lists[] = {computeCmdList_.Get()};
        computeQueue_->ExecuteCommandLists(1, lists);

        if (token.has_value()) {
            sequencer.SignalComputeComplete(*token, computeQueue_.Get(), computeFence_.Get());
            sequencer.QueueConsumerAfterCompute(*token, directQueue_.Get(), computeFence_.Get());
        }
    }
}


} // namespace nrfusion::testing
