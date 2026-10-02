#include "nrfusion/D3D12CommandStateRestore.hpp"

namespace nrfusion {

bool RestoreD3D12CommandState(
    ID3D12GraphicsCommandList* commandList,
    const D3D12CommandStateRestore* restore) noexcept {
    return restore == nullptr || restore->Apply(commandList);
}

} // namespace nrfusion
