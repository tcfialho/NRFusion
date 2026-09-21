#include "D3D12TestHarness.hpp"

namespace nrfusion::testing {

namespace {

float Halton(std::uint32_t index, std::uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

} // namespace

bool D3D12TestHarness::IsSupported(const nrfusion::GameContext& game) const {
    return game.api == GraphicsApi::D3D12;
}

nrfusion::FrameContext D3D12TestHarness::AcquireFrame(const nrfusion::ProviderInput& input) {
    nrfusion::FrameContext frame{};
    frame.frameId = input.frameId;
    frame.api = GraphicsApi::D3D12;

    frame.color.opaqueId = reinterpret_cast<std::uintptr_t>(colorBuffer_.Get());
    frame.color.resolution = {config_.width, config_.height};
    frame.color.format = ResourceFormat::Rgba16Float;

    frame.depth.opaqueId = reinterpret_cast<std::uintptr_t>(depthBuffer_.Get());
    frame.depth.resolution = {config_.width, config_.height};
    frame.depth.format = ResourceFormat::D32Float;
    frame.depthReliable = true;

    frame.motionVectors.opaqueId = reinterpret_cast<std::uintptr_t>(motionBuffer_.Get());
    frame.motionVectors.resolution = {config_.width, config_.height};
    frame.motionVectors.format = ResourceFormat::Rg16Float;
    frame.motionVectorSource = MotionSource::Native;
    frame.motionVectorsReliable = true;

    frame.exposure.opaqueId = reinterpret_cast<std::uintptr_t>(exposureBuffer_.Get());
    frame.exposure.resolution = {1, 1};
    frame.exposure.format = ResourceFormat::R32Float;

    frame.reactiveMask.opaqueId = reinterpret_cast<std::uintptr_t>(reactiveBuffer_.Get());
    frame.reactiveMask.resolution = {config_.width, config_.height};
    frame.reactiveMask.format = ResourceFormat::R8Unorm;

    frame.renderResolution = {config_.width, config_.height};
    frame.outputResolution = {config_.width, config_.height};
    frame.hdr = true;

    const auto phase = static_cast<std::uint32_t>(input.frameId % 16);
    frame.jitter.x = (Halton(phase + 1, 2) - 0.5f) / static_cast<float>(config_.width);
    frame.jitter.y = (Halton(phase + 1, 3) - 0.5f) / static_cast<float>(config_.height);
    frame.cameraCut = (input.frameId == 45 || input.frameId == 90);

    return frame;
}

nrfusion::ProviderDiagnostics D3D12TestHarness::Diagnostics() const {
    nrfusion::ProviderDiagnostics diag{};
    diag.supported = true;
    diag.frameComplete = true;
    diag.depthValid = depthBuffer_ != nullptr;
    diag.motionValid = motionBuffer_ != nullptr;
    diag.exposureValid = exposureBuffer_ != nullptr;
    return diag;
}


} // namespace nrfusion::testing
