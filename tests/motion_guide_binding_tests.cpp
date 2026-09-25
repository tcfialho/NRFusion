#include "nrfusion/MotionGuideBinding.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

FrameContext Frame() {
    FrameContext frame{};
    frame.frameId = 42;
    frame.api = GraphicsApi::D3D12;
    frame.renderResolution = {1920, 1080};
    frame.outputResolution = {1920, 1080};
    return frame;
}

MotionGuideBinding Binding(MotionSource source) {
    MotionGuideBinding binding{};
    binding.source = source;
    binding.resource = {
        7, {320, 180}, ResourceFormat::Rg16Float};
    binding.reliability = ResourceReliability::Reliable;
    binding.ownership = ResourceOwnership::ProviderOwned;
    binding.lifetime = ResourceLifetime::UntilNextAcquire;
    binding.sourceFrameId = 42;
    return binding;
}

} // namespace

int main() {
    auto frame = Frame();

    const auto nvof = Binding(
        MotionSource::NvidiaOpticalFlow);
    assert(BindMotionGuide(frame, nvof));
    assert(frame.EffectiveMotionSource() ==
           MotionSource::NvidiaOpticalFlow);
    assert(frame.MotionReliable(
        MotionSource::NvidiaOpticalFlow));
    assert(frame.motionVectors.provenance ==
           ResourceProvenance::OpticalFlow);
    assert(frame.motionVectors.ownership ==
           ResourceOwnership::ProviderOwned);
    assert(frame.motionVectors.lifetime ==
           ResourceLifetime::UntilNextAcquire);
    assert(frame.motionVectors.sourceFrameId == frame.frameId);

    auto shader = Binding(MotionSource::ShaderEstimated);
    shader.resource.provenance =
        ResourceProvenance::GameNative;
    assert(BindMotionGuide(frame, shader).failure ==
           MotionGuideBindingFailure::ProvenanceMismatch);

    auto incomplete = Binding(
        MotionSource::NvidiaOpticalFlow);
    incomplete.ownership = ResourceOwnership::Unknown;
    assert(BindMotionGuide(frame, incomplete).failure ==
           MotionGuideBindingFailure::MissingEvidence);

    auto stale = Binding(MotionSource::DlssContract);
    stale.sourceFrameId = 41;
    assert(BindMotionGuide(frame, stale).failure ==
           MotionGuideBindingFailure::SourceFrameMismatch);

    MotionGuideBinding zero{};
    zero.source = MotionSource::Zero;
    assert(BindMotionGuide(frame, zero));
    assert(!frame.motionVectors.Valid());
    assert(frame.EffectiveMotionSource() == MotionSource::Zero);

    zero.resource = {
        9, {1920, 1080}, ResourceFormat::Rg16Float};
    assert(BindMotionGuide(frame, zero).failure ==
           MotionGuideBindingFailure::InvalidZeroBinding);

    FrameContext invalid{};
    assert(BindMotionGuide(invalid, nvof).failure ==
           MotionGuideBindingFailure::InvalidFrame);
    return 0;
}
