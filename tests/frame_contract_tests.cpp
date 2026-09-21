#include "nrfusion/FrameContract.hpp"
#include "nrfusion/FrameContractProvider.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

ResourceRef Color(FrameId frameId = 0) {
    ResourceRef ref{1, {1920, 1080}, ResourceFormat::Rgba16Float};
    ref.provenance = ResourceProvenance::GameNative;
    ref.reliability = ResourceReliability::Reliable;
    ref.ownership = ResourceOwnership::Borrowed;
    ref.lifetime = ResourceLifetime::Frame;
    ref.sourceFrameId = frameId;
    return ref;
}

FrameContext BaseFrame() {
    FrameContext frame{};
    frame.frameId = 10;
    frame.api = GraphicsApi::D3D12;
    frame.color = Color(10);
    frame.renderResolution = {1920, 1080};
    frame.outputResolution = {3840, 2160};
    return frame;
}

void TestReadyContract() {
    auto frame = BaseFrame();
    assert(frame.ReadyForCore());
    assert(frame.color.EvidenceExplicit());
}

void TestFrameIdentityRejectsFutureResource() {
    auto frame = BaseFrame();
    frame.color.sourceFrameId = 11;
    assert(!frame.ReadyForCore());

    frame = BaseFrame();
    frame.depth = {2, {1920, 1080}, ResourceFormat::D32Float};
    frame.depth.sourceFrameId = 11;
    assert(!frame.ReadyForCore());
}

void TestExplicitReliabilityOverridesLegacyFlags() {
    auto frame = BaseFrame();
    frame.depth = {2, {1920, 1080}, ResourceFormat::D32Float};
    frame.depthReliable = true;
    frame.depth.reliability = ResourceReliability::Unreliable;
    assert(!frame.DepthReliable());

    frame.depth.reliability = ResourceReliability::Reliable;
    frame.depthReliable = false;
    assert(frame.DepthReliable());
}

void TestMotionProvenanceIsAuthoritative() {
    auto frame = BaseFrame();
    frame.motionVectors = {3, {1920, 1080}, ResourceFormat::Rg16Float};
    frame.motionVectors.reliability = ResourceReliability::Reliable;
    frame.motionVectors.provenance = ResourceProvenance::Generated;
    frame.motionVectorSource = MotionSource::Native;
    frame.motionVectorsReliable = true;

    assert(!frame.HasNativeMotion());
    assert(!frame.MotionReliable(MotionSource::Native));
    assert(frame.MotionReliable(MotionSource::Zero));

    frame.motionVectors.provenance = ResourceProvenance::DlssContract;
    assert(frame.HasDlssContractMotion());
    assert(frame.MotionReliable(MotionSource::DlssContract));
}

void TestLegacyContractRemainsCompatible() {
    FrameContext frame{};
    frame.frameId = 1;
    frame.api = GraphicsApi::D3D12;
    frame.color = {1, {1280, 720}, ResourceFormat::Rgba16Float};
    frame.renderResolution = {1280, 720};
    frame.outputResolution = {1920, 1080};
    frame.depth = {2, {1280, 720}, ResourceFormat::D32Float};
    frame.depthReliable = true;
    frame.motionVectors = {3, {1280, 720}, ResourceFormat::Rg16Float};
    frame.motionVectorSource = MotionSource::Native;
    frame.motionVectorsReliable = true;

    assert(frame.ReadyForCore());
    assert(frame.DepthReliable());
    assert(frame.MotionReliable(MotionSource::Native));
}

void TestProviderIdentityFieldsAreIndependent() {
    ProviderInput input{7, 1001, 42, 9};
    assert(input.frameId == 7);
    assert(input.hostFrameToken == 1001);
    assert(input.viewId == 42);
    assert(input.configurationGeneration == 9);
}

} // namespace

int main() {
    TestReadyContract();
    TestFrameIdentityRejectsFutureResource();
    TestExplicitReliabilityOverridesLegacyFlags();
    TestMotionProvenanceIsAuthoritative();
    TestLegacyContractRemainsCompatible();
    TestProviderIdentityFieldsAreIndependent();
    return 0;
}
