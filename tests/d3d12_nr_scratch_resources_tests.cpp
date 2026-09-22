#include "nrfusion/D3D12NrScratchResources.hpp"

#include <cassert>

int main() {
    using namespace nrfusion;

    D3D12NrScratchResources scratch;
    NrDeferredRetirementQueue retirement;

    const D3D12NrScratchDesc valid{
        DXGI_FORMAT_R16G16B16A16_FLOAT, 1920, 1080, 1280, 720};
    const D3D12NrScratchDesc invalid{
        DXGI_FORMAT_UNKNOWN, 1920, 1080, 1280, 720};

    assert(!scratch.Complete());
    assert(!scratch.Matches(valid));
    assert(scratch.Get(D3D12NrScratchKind::Output) == nullptr);
    assert(scratch.Get(D3D12NrScratchKind::ColorCopy) == nullptr);
    assert(scratch.Get(D3D12NrScratchKind::HdrCopy) == nullptr);
    assert(scratch.State(D3D12NrScratchKind::Output) ==
           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    assert(!scratch.Ensure(nullptr, valid, retirement));
    assert(!scratch.Ensure(nullptr, invalid, retirement));
    assert(retirement.Size() == 0);

    assert(!scratch.Transition(
        nullptr, D3D12NrScratchKind::Output,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    assert(scratch.Retire(retirement));
    assert(retirement.Size() == 0);

    scratch.ReleaseAfterIdle();
    assert(!scratch.Complete());

    return 0;
}
