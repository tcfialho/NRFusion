#pragma once

#include "nrfusion/D3D12NrExecutor.hpp"
#include "nrfusion/RuntimeAdvancedConfig.hpp"

namespace nrfusion {

void TryInstallGameNeuralHooks(HMODULE module);
class ScopedGameNeuralHookBypass {
public:
    ScopedGameNeuralHookBypass() noexcept;
    ~ScopedGameNeuralHookBypass();
    ScopedGameNeuralHookBypass(const ScopedGameNeuralHookBypass&) = delete;
    ScopedGameNeuralHookBypass& operator=(const ScopedGameNeuralHookBypass&) = delete;
private:
    bool previous_;
};
struct GameNeuralFrameContext {
    RuntimeAdvancedConfig advanced{};
    unsigned int createFlags = 0;
    std::uint64_t epoch = 0;
    bool rayReconstruction = false;
    bool beforeUpscale = false;
    bool runBeforeUpscale = false;
    float workingScale = 1;
};
D3D12NrFrameResult ExecuteGameNeuralFrame(D3D12NrExecutor& executor,
    ID3D12GraphicsCommandList* commands, NgxParameter* parameters,
    const GameNeuralFrameContext& context);

} // namespace nrfusion
