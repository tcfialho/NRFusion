#include "D3D9ExShareHarness.hpp"

namespace nrfusion::test {

bool D3D9ExShareHarness::ProveAcquireContract() {
    if (!device9_ || !gameSource9_ ||
        width_ == 0 || height_ == 0) {
        return false;
    }

    ComPtr<IDirect3DSurface9> source;
    if (FAILED(gameSource9_->GetSurfaceLevel(
            0, &source))) {
        return false;
    }

    D3D9ExNativeAcquireInput input{};
    input.identity.frameId = 101;
    input.identity.hostFrameToken = 202;
    input.identity.viewId = 303;
    input.identity.configurationGeneration = 4;
    input.device = device9_.Get();
    input.color = source.Get();
    input.jitter = {0.25f, -0.25f};
    input.hdr = true;

    const auto acquired =
        AcquireD3D9ExNativeFrame(input);
    if (!acquired ||
        acquired.frame.api != GraphicsApi::D3D9 ||
        acquired.frame.frameId != input.identity.frameId ||
        acquired.frame.configurationGeneration !=
            input.identity.configurationGeneration ||
        acquired.frame.color.opaqueId !=
            reinterpret_cast<std::uint64_t>(source.Get()) ||
        acquired.frame.color.resolution !=
            Resolution{width_, height_} ||
        acquired.frame.color.format !=
            ResourceFormat::Rgba16Float ||
        acquired.frame.color.provenance !=
            ResourceProvenance::GameNative ||
        acquired.frame.color.ownership !=
            ResourceOwnership::Borrowed ||
        acquired.frame.color.lifetime !=
            ResourceLifetime::Frame ||
        acquired.frame.depth.Valid() ||
        acquired.frame.motionVectors.Valid()) {
        return false;
    }

    auto missingColor = input;
    missingColor.color = nullptr;
    return AcquireD3D9ExNativeFrame(missingColor).failure ==
           D3D9ExNativeAcquireFailure::MissingColor;
}

} // namespace nrfusion::test
