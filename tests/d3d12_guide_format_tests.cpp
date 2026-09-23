#include "nrfusion/D3D12GuideFormat.hpp"

#include <cassert>

using namespace nrfusion;

int main() {
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Depth,
               D3D12TypelessGuideFamily::R32) ==
           ResourceFormat::D32Float);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Depth,
               D3D12TypelessGuideFamily::R16) ==
           ResourceFormat::R16Unorm);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Depth,
               D3D12TypelessGuideFamily::R24G8) ==
           ResourceFormat::R24UnormX8);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Depth,
               D3D12TypelessGuideFamily::R32G8X24) ==
           ResourceFormat::R32FloatX8X24);

    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Motion,
               D3D12TypelessGuideFamily::R32G32) ==
           ResourceFormat::Rg32Float);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Motion,
               D3D12TypelessGuideFamily::R16G16) ==
           ResourceFormat::Rg16Float);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Motion,
               D3D12TypelessGuideFamily::R8G8B8A8) ==
           ResourceFormat::Rgba8Unorm);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Motion,
               D3D12TypelessGuideFamily::R16G16B16A16) ==
           ResourceFormat::Rgba16Float);

    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Depth,
               D3D12TypelessGuideFamily::R16G16) ==
           ResourceFormat::Unknown);
    assert(NormalizeD3D12TypelessGuideFormat(
               D3D12GuideRole::Motion,
               D3D12TypelessGuideFamily::R32) ==
           ResourceFormat::Unknown);
    return 0;
}
