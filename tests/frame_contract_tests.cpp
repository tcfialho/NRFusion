#include "nrfusion/FrameContract.hpp"
#include "nrfusion/FrameContractProvider.hpp"

#include <cassert>
#include <type_traits>

using namespace nrfusion;

namespace {

static_assert(std::is_trivially_copyable_v<ResourceRef>);
static_assert(std::is_standard_layout_v<ResourceRef>);
static_assert(std::is_trivially_copyable_v<FrameContext>);
static_assert(std::is_standard_layout_v<FrameContext>);

class FakeProvider final : public IFrameContractProvider {
public:
    explicit FakeProvider(FrameContext frame) : frame_(frame) {}

    bool IsSupported(const GameContext&) const override { return true; }

    FrameContext AcquireFrame(const ProviderInput& input) override {
        FrameContext out = frame_;
        out.frameId = input.frameId;
        out.hostFrameToken = input.hostFrameToken;
        out.viewId = input.viewId;
        out.configurationGeneration = input.configurationGeneration;
        return out;
    }

    ProviderDiagnostics Diagnostics() const override {
        return {true, frame_.ReadyForCore(), frame_.HasDepth(),
                frame_.motionVectors.Valid(), frame_.HasExposure()};
    }

private:
    FrameContext frame_{};
};

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

void TestPartialEvidenceFailsClosed() {
    auto frame = BaseFrame();
    frame.color.ownership = ResourceOwnership::Unknown;
    assert(!frame.color.EvidenceWellFormed());
    assert(!frame.ReadyForCore());
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
    frame.depth.provenance = ResourceProvenance::GameNative;
    frame.depth.reliability = ResourceReliability::Unreliable;
    frame.depth.ownership = ResourceOwnership::Borrowed;
    frame.depth.lifetime = ResourceLifetime::Session;
    frame.depth.sourceFrameId = frame.frameId;
    frame.depthReliable = true;
    assert(!frame.DepthReliable());

    frame.depth.reliability = ResourceReliability::Reliable;
    frame.depthReliable = false;
    assert(frame.DepthReliable());
}

void TestPartialGuideEvidenceCannotInfluencePolicy() {
    auto frame = BaseFrame();
    frame.depth = {2, {1920, 1080}, ResourceFormat::D32Float};
    frame.depth.reliability = ResourceReliability::Reliable;
    frame.depthReliable = true;
    assert(!frame.DepthReliable());

    frame.motionVectors = {3, {1920, 1080}, ResourceFormat::Rg16Float};
    frame.motionVectors.provenance = ResourceProvenance::GameNative;
    frame.motionVectors.reliability = ResourceReliability::Reliable;
    frame.motionVectorsReliable = true;
    assert(!frame.HasNativeMotion());
    assert(!frame.MotionReliable(MotionSource::Native));
}

void TestMotionProvenanceIsAuthoritative() {
    auto frame = BaseFrame();
    frame.motionVectors = {3, {1920, 1080}, ResourceFormat::Rg16Float};
    frame.motionVectors.reliability = ResourceReliability::Reliable;
    frame.motionVectors.provenance = ResourceProvenance::Generated;
    frame.motionVectors.ownership = ResourceOwnership::Borrowed;
    frame.motionVectors.lifetime = ResourceLifetime::Session;
    frame.motionVectors.sourceFrameId = frame.frameId;
    frame.motionVectorSource = MotionSource::Native;
    frame.motionVectorsReliable = true;

    assert(!frame.HasNativeMotion());
    assert(!frame.MotionReliable(MotionSource::Native));
    assert(frame.MotionReliable(MotionSource::Zero));

    frame.motionVectors.provenance = ResourceProvenance::DlssContract;
    assert(frame.HasDlssContractMotion());
    assert(frame.MotionReliable(MotionSource::DlssContract));
}

void TestExplicitEvidenceRequiresFrameBinding() {
    auto frame = BaseFrame();
    frame.color.sourceFrameId = 0;
    assert(!frame.color.EvidenceWellFormed());
    assert(!frame.ReadyForCore());
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

void TestProvenanceStress() {
    for (FrameId id = 1; id <= 100000; ++id) {
        auto frame = BaseFrame();
        frame.frameId = id;
        frame.color.sourceFrameId = id;
        frame.resetHistory = (id % 97) == 0;

        frame.motionVectors = {3, {1920, 1080}, ResourceFormat::Rg16Float};
        frame.motionVectors.provenance = (id & 1) ? ResourceProvenance::GameNative
                                                  : ResourceProvenance::DlssContract;
        frame.motionVectors.reliability = ResourceReliability::Reliable;
        frame.motionVectors.ownership = ResourceOwnership::Borrowed;
        frame.motionVectors.lifetime = ResourceLifetime::Frame;
        frame.motionVectors.sourceFrameId = id;

        assert(frame.ReadyForCore());
        const auto expected = (id & 1) ? MotionSource::Native : MotionSource::DlssContract;
        assert(frame.MotionReliable(expected));
    }
}

void TestFakeProviderContracts() {
    FakeProvider valid(BaseFrame());
    ProviderInput input{10, 1001, 42, 9};
    const auto acquired = valid.AcquireFrame(input);
    assert(acquired.ReadyForCore());
    assert(acquired.hostFrameToken == 1001);
    assert(acquired.viewId == 42);
    assert(acquired.configurationGeneration == 9);

    auto incomplete = BaseFrame();
    incomplete.color = {};
    assert(!FakeProvider(incomplete).AcquireFrame(input).ReadyForCore());

    auto contradictory = BaseFrame();
    contradictory.color.sourceFrameId = 11;
    assert(!FakeProvider(contradictory).AcquireFrame(input).ReadyForCore());
}

} // namespace

int main() {
    TestReadyContract();
    TestPartialEvidenceFailsClosed();
    TestFrameIdentityRejectsFutureResource();
    TestExplicitReliabilityOverridesLegacyFlags();
    TestPartialGuideEvidenceCannotInfluencePolicy();
    TestMotionProvenanceIsAuthoritative();
    TestExplicitEvidenceRequiresFrameBinding();
    TestLegacyContractRemainsCompatible();
    TestProvenanceStress();
    TestFakeProviderContracts();
    return 0;
}
