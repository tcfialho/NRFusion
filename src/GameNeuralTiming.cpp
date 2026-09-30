#include "GameNeuralTiming.hpp"
#include "nrfusion/Logger.hpp"
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>

namespace nrfusion {
using Microsoft::WRL::ComPtr;
constexpr UINT kTimingSlots = 4;
constexpr UINT kQueriesPerSlot = 4;

struct GameNeuralTimingState {
    struct Slot {
        ComPtr<ID3D12Fence> fence;
        UINT64 completion = 0;
        UINT64 frequency = 0;
        GameNeuralGpuSample sample{};
        bool active = false;
        bool submitted = false;
        bool applied = false;
    };
    std::mutex mutex;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    std::array<Slot, kTimingSlots> slots;
};

namespace {
struct PendingTiming {
    ID3D12CommandList* commands;
    std::shared_ptr<GameNeuralTimingState> state;
    UINT slot;
};
struct TimingRegistry {
    TimingRegistry() { pending.reserve(32); submitted.reserve(32); }
    std::mutex mutex;
    std::vector<PendingTiming> pending;
    std::vector<std::shared_ptr<GameNeuralTimingState>> submitted;
    std::atomic<bool> active{false};
};
TimingRegistry& Registry() { static auto* registry = new TimingRegistry; return *registry; }

bool Completed(const GameNeuralTimingState::Slot& slot) {
    return slot.submitted && slot.fence->GetCompletedValue() >= slot.completion;
}

void ReleaseCompleted(TimingRegistry& registry) {
    std::erase_if(registry.submitted, [](const auto& state) {
        std::lock_guard lock(state->mutex);
        return std::none_of(state->slots.begin(), state->slots.end(),
            [](const auto& slot) { return slot.active && slot.submitted && !Completed(slot); });
    });
    registry.active.store(!registry.pending.empty() || !registry.submitted.empty());
}
} // namespace

bool GameNeuralTiming::Initialize(ID3D12Device* device) {
    if (state_) return true;
    if (!device) return false;
    auto state = std::make_shared<GameNeuralTimingState>();
    D3D12_QUERY_HEAP_DESC queries{};
    queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queries.Count = kTimingSlots * kQueriesPerSlot;
    if (FAILED(device->CreateQueryHeap(&queries, IID_PPV_ARGS(&state->queries)))) return false;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = queries.Count * sizeof(UINT64);
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&state->readback)))) return false;
    for (auto& slot : state->slots)
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&slot.fence)))) return false;
    state_ = state;
    return true;
}

std::uint32_t GameNeuralTiming::Begin(ID3D12GraphicsCommandList* commands, std::uint64_t generation, float scale) {
    if (!state_ || !commands) return UINT32_MAX;
    auto& registry = Registry();
    std::lock_guard registryLock(registry.mutex);
    if (registry.pending.size() >= 32) return UINT32_MAX;
    std::lock_guard stateLock(state_->mutex);
    for (UINT index = 0; index < kTimingSlots; ++index) {
        auto& slot = state_->slots[index];
        if (slot.active) continue;
        slot.active = true;
        slot.submitted = slot.applied = false;
        slot.sample = {generation, scale, 0};
        ComPtr<ID3D12CommandList> identity;
        if (FAILED(commands->QueryInterface(IID_PPV_ARGS(&identity)))) { slot.active = false; return UINT32_MAX; }
        registry.pending.push_back({identity.Get(), state_, index});
        registry.active.store(true);
        return index;
    }
    return UINT32_MAX;
}

void GameNeuralTiming::Mark(ID3D12GraphicsCommandList* commands, std::uint32_t slot, std::uint32_t index) {
    if (!state_ || slot >= kTimingSlots || index >= kQueriesPerSlot) return;
    commands->EndQuery(state_->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * kQueriesPerSlot + index);
}

void GameNeuralTiming::Finish(ID3D12GraphicsCommandList* commands, std::uint32_t index, bool applied) {
    if (!state_ || index >= kTimingSlots) return;
    {
        std::lock_guard lock(state_->mutex);
        state_->slots[index].applied = applied;
    }
    commands->ResolveQueryData(state_->queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        index * kQueriesPerSlot, kQueriesPerSlot, state_->readback.Get(), index * kQueriesPerSlot * sizeof(UINT64));
}

void SubmitNeuralTimings(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* commands) {
    auto& registry = Registry();
    if (!registry.active.load()) return;
    std::lock_guard registryLock(registry.mutex);
    for (size_t index = 0; index < registry.pending.size();) {
        const auto& pending = registry.pending[index];
        bool matches = false;
        for (UINT command = 0; command < count; ++command) matches |= commands[command] == pending.commands;
        if (!matches) { ++index; continue; }
        const auto state = pending.state;
        {
            std::lock_guard stateLock(state->mutex);
            auto& slot = state->slots[pending.slot];
            if (FAILED(queue->GetTimestampFrequency(&slot.frequency)) ||
                FAILED(queue->Signal(slot.fence.Get(), ++slot.completion))) {
                NRF_LOG_ERROR("NeuralTiming", "Queue timing submission failed; Auto sample unavailable");
                slot.completion = UINT64_MAX;
                slot.frequency = 0;
                slot.submitted = true;
            } else {
                slot.submitted = true;
            }
        }
        registry.submitted.push_back(state);
        registry.pending.erase(registry.pending.begin() + static_cast<std::ptrdiff_t>(index));
    }
    ReleaseCompleted(registry);
}

bool HasPendingNeuralTimings() noexcept { return Registry().active.load(); }

bool GameNeuralTiming::Consume(GameNeuralGpuSample& sample) {
    if (!state_) return false;
    std::lock_guard lock(state_->mutex);
    for (UINT index = 0; index < kTimingSlots; ++index) {
        auto& slot = state_->slots[index];
        if (!slot.active || !Completed(slot)) continue;
        slot.active = false;
        if (!slot.applied || !slot.frequency) continue;
        const SIZE_T offset = index * kQueriesPerSlot * sizeof(UINT64);
        const D3D12_RANGE range{offset, offset + kQueriesPerSlot * sizeof(UINT64)};
        void* mapped = nullptr;
        if (FAILED(state_->readback->Map(0, &range, &mapped))) continue;
        const auto* ticks = reinterpret_cast<const UINT64*>(static_cast<const BYTE*>(mapped) + offset);
        const UINT64 elapsed = (ticks[1] >= ticks[0] ? ticks[1] - ticks[0] : 0) +
                               (ticks[3] >= ticks[2] ? ticks[3] - ticks[2] : 0);
        sample = slot.sample;
        sample.gpuMs = elapsed * 1000.0 / slot.frequency;
        const D3D12_RANGE written{0, 0};
        state_->readback->Unmap(0, &written);
        return sample.gpuMs > 0;
    }
    return false;
}
} // namespace nrfusion
