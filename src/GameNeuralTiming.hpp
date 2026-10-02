#pragma once
#include <d3d12.h>
#include <cstdint>
#include <memory>

namespace nrfusion {
struct GameNeuralTimingState;

struct GameNeuralGpuSample {
    std::uint64_t generation = 0;
    float workingScale = 1;
    double gpuMs = 0;
};

class GameNeuralTiming {
public:
    bool Initialize(ID3D12Device* device);
    std::uint32_t Begin(ID3D12GraphicsCommandList* commands, std::uint64_t generation, float scale);
    void Mark(ID3D12GraphicsCommandList* commands, std::uint32_t slot, std::uint32_t index);
    void Finish(ID3D12GraphicsCommandList* commands, std::uint32_t slot, bool applied);
    bool Consume(GameNeuralGpuSample& sample);
private:
    std::shared_ptr<GameNeuralTimingState> state_;
};

void RegisterNeuralCommandQueue(ID3D12CommandQueue* queue);
void SubmitNeuralTimings(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* commands);
bool HasPendingNeuralTimings() noexcept;
} // namespace nrfusion
