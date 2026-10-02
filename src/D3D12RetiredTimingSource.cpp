#include "nrfusion/D3D12RetiredTimingSource.hpp"

namespace nrfusion {

bool D3D12RetiredTimingSource::BelongsToBoundDevice(
    ID3D12DeviceChild* child) const noexcept {
    if (!device_ || child == nullptr) return false;
    ID3D12Device* actual = nullptr;
    const HRESULT hr = child->GetDevice(IID_PPV_ARGS(&actual));
    const bool matches = SUCCEEDED(hr) && actual == device_.Get();
    if (actual != nullptr) actual->Release();
    return matches;
}

bool D3D12RetiredTimingSource::BindAfterIdle(
    ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (device == nullptr || queue == nullptr) return false;

    ID3D12Device* queueDevice = nullptr;
    const HRESULT queueDeviceHr = queue->GetDevice(IID_PPV_ARGS(&queueDevice));
    const bool sameDevice = SUCCEEDED(queueDeviceHr) && queueDevice == device;
    if (queueDevice != nullptr) queueDevice->Release();
    if (!sameDevice) return false;

    if (device_.Get() == device && queue_.Get() == queue && queries_ && readback_)
        return true;

    ResetAfterIdle();

    UINT64 frequency = 0;
    if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0)
        return false;

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = static_cast<UINT>(kCapacity * 2);

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries;
    if (FAILED(device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&queries))))
        return false;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = kCapacity * 2 * sizeof(UINT64);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback))))
        return false;

    device_ = device;
    queue_ = queue;
    queries_ = std::move(queries);
    readback_ = std::move(readback);
    timestampFrequency_ = frequency;
    return true;
}

void D3D12RetiredTimingSource::ResetAfterIdle() noexcept {
    for (auto& entry : entries_) entry = {};
    recordingCommands_ = nullptr;
    recordingSlot_ = 0;
    head_ = 0;
    size_ = 0;
    timestampFrequency_ = 0;
    lastCompletionValue_ = 0;
    readback_.Reset();
    queries_.Reset();
    queue_.Reset();
    device_.Reset();
}

bool D3D12RetiredTimingSource::BeginSample(
    ID3D12GraphicsCommandList* commands, NrRetiredTimingSample sample) noexcept {
    if (!queries_ || commands == nullptr || recordingCommands_ != nullptr ||
        size_ == kCapacity || !BelongsToBoundDevice(commands))
        return false;

    const std::size_t slot = (head_ + size_) % kCapacity;
    const UINT query = static_cast<UINT>(slot * 2);
    commands->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query);
    entries_[slot] = {sample, 0, false};
    recordingSlot_ = slot;
    recordingCommands_ = commands;
    ++size_;
    return true;
}

bool D3D12RetiredTimingSource::Begin(
    ID3D12GraphicsCommandList* commands, const WorkTicket& ticket) noexcept {
    if (ticket.id == 0 || ticket.session == 0) return false;
    return BeginSample(commands, {true, ticket, 0.0});
}

bool D3D12RetiredTimingSource::BeginInvalid(
    ID3D12GraphicsCommandList* commands) noexcept {
    return BeginSample(commands, {});
}

bool D3D12RetiredTimingSource::End(
    ID3D12GraphicsCommandList* commands,
    std::uint64_t completionValue) noexcept {
    if (recordingCommands_ == nullptr || commands != recordingCommands_ ||
        completionValue == 0 || completionValue <= lastCompletionValue_)
        return false;

    Entry& entry = entries_[recordingSlot_];
    const UINT query = static_cast<UINT>(recordingSlot_ * 2);
    commands->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1);
    commands->ResolveQueryData(
        queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 2,
        readback_.Get(), static_cast<UINT64>(query) * sizeof(UINT64));
    entry.completionValue = completionValue;
    entry.pending = true;
    lastCompletionValue_ = completionValue;
    recordingCommands_ = nullptr;
    return true;
}

bool D3D12RetiredTimingSource::EndInvalid(
    ID3D12GraphicsCommandList* commands,
    std::uint64_t completionValue) noexcept {
    if (recordingCommands_ == nullptr || commands != recordingCommands_)
        return false;
    entries_[recordingSlot_].sample = {};
    return End(commands, completionValue);
}

void D3D12RetiredTimingSource::PopFront() noexcept {
    entries_[head_] = {};
    head_ = (head_ + 1) % kCapacity;
    --size_;
    if (size_ == 0) head_ = 0;
}

std::optional<NrRetiredTimingSample> D3D12RetiredTimingSource::TryRetire(
    std::uint64_t completedValue) noexcept {
    if (size_ == 0) return std::nullopt;
    Entry& entry = entries_[head_];
    if (!entry.pending || entry.completionValue > completedValue)
        return std::nullopt;

    NrRetiredTimingSample sample = entry.sample;
    if (sample.mapsWork) {
        void* mapped = nullptr;
        const UINT query = static_cast<UINT>(head_ * 2);
        const SIZE_T begin = static_cast<SIZE_T>(query) * sizeof(UINT64);
        const D3D12_RANGE readRange{begin, begin + 2 * sizeof(UINT64)};
        if (FAILED(readback_->Map(0, &readRange, &mapped))) return std::nullopt;
        const auto* ticks = static_cast<const UINT64*>(mapped);
        const UINT64 start = ticks[query];
        const UINT64 end = ticks[query + 1];
        const D3D12_RANGE writtenRange{0, 0};
        readback_->Unmap(0, &writtenRange);
        if (end > start) {
            sample.gpuMs =
                static_cast<double>(end - start) * 1000.0 /
                static_cast<double>(timestampFrequency_);
        }
    }

    PopFront();
    return sample;
}

}
