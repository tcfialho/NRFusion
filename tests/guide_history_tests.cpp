#include "nrfusion/GuideHistoryState.hpp"
#include "nrfusion/TemporalHistoryRegistry.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

FrameContext BaseFrame() {
    FrameContext frame{};
    frame.frameId = 10;
    frame.configurationGeneration = 1;
    frame.api = GraphicsApi::D3D12;
    frame.renderResolution = {1920, 1080};
    frame.outputResolution = {1920, 1080};

    frame.color = {
        1, {1920, 1080}, ResourceFormat::Rgba16Float};
    frame.color.provenance =
        ResourceProvenance::GameNative;
    frame.color.reliability =
        ResourceReliability::Reliable;
    frame.color.ownership =
        ResourceOwnership::Borrowed;
    frame.color.lifetime =
        ResourceLifetime::Frame;
    frame.color.sourceFrameId = frame.frameId;

    frame.depth = {
        2, {1920, 1080}, ResourceFormat::D32Float};
    frame.depth.provenance =
        ResourceProvenance::GameNative;
    frame.depth.reliability =
        ResourceReliability::Reliable;
    frame.depth.ownership =
        ResourceOwnership::Borrowed;
    frame.depth.lifetime =
        ResourceLifetime::Frame;
    frame.depth.sourceFrameId = frame.frameId;

    frame.motionVectors = {
        3, {1920, 1080}, ResourceFormat::Rg16Float};
    frame.motionVectors.provenance =
        ResourceProvenance::GameNative;
    frame.motionVectors.reliability =
        ResourceReliability::Reliable;
    frame.motionVectors.ownership =
        ResourceOwnership::Borrowed;
    frame.motionVectors.lifetime =
        ResourceLifetime::Frame;
    frame.motionVectors.sourceFrameId = frame.frameId;
    return frame;
}

} // namespace

int main() {
    ViewDescriptor view{};
    view.featureKey = 77;
    view.viewKey = 3;
    view.width = 1920;
    view.height = 1080;
    view.outputWidth = 1920;
    view.outputHeight = 1080;

    TemporalHistoryRegistry histories;
    auto frame = BaseFrame();
    auto native = DescribeGuideHistory(
        frame, MotionSource::Native);

    const auto first = histories.Acquire(view, 10, native);
    assert(first.resetRequired);

    frame.frameId = 11;
    frame.color.sourceFrameId = 11;
    frame.depth.sourceFrameId = 11;
    frame.motionVectors.sourceFrameId = 11;
    native = DescribeGuideHistory(
        frame, MotionSource::Native);
    const auto stable = histories.Acquire(view, 11, native);
    assert(!stable.resetRequired);
    assert(stable.historyId == first.historyId);

    const auto nvof = DescribeGuideHistory(
        frame, MotionSource::NvidiaOpticalFlow);
    const auto sourceChanged =
        histories.Acquire(view, 12, nvof);
    assert(sourceChanged.resetRequired);
    assert(sourceChanged.historyId != stable.historyId);

    frame.depth.reliability =
        ResourceReliability::Unreliable;
    const auto degraded = DescribeGuideHistory(
        frame, MotionSource::NvidiaOpticalFlow);
    const auto reliabilityChanged =
        histories.Acquire(view, 13, degraded);
    assert(reliabilityChanged.resetRequired);

    frame.cameraCut = true;
    const auto cut = DescribeGuideHistory(
        frame, MotionSource::Zero);
    const auto cutReset = histories.Acquire(view, 14, cut);
    assert(cutReset.resetRequired);
    const auto sameCut = histories.Acquire(view, 14, cut);
    assert(sameCut.resetRequired);
    assert(sameCut.historyId == cutReset.historyId);

    frame.cameraCut = false;
    const auto afterCut = DescribeGuideHistory(
        frame, MotionSource::NvidiaOpticalFlow);
    const auto recovered =
        histories.Acquire(view, 15, afterCut);
    assert(!recovered.resetRequired);
    assert(recovered.historyId == cutReset.historyId);

    frame.exposure = {
        4, {1, 1}, ResourceFormat::R32Float};
    frame.exposure.provenance =
        ResourceProvenance::GameNative;
    frame.exposure.reliability =
        ResourceReliability::Reliable;
    frame.exposure.ownership =
        ResourceOwnership::Borrowed;
    frame.exposure.lifetime =
        ResourceLifetime::Frame;
    frame.exposure.sourceFrameId = frame.frameId;
    const auto exposureAppeared = DescribeGuideHistory(
        frame, MotionSource::NvidiaOpticalFlow);
    assert(histories.Acquire(
        view, 16, exposureAppeared).resetRequired);

    frame.configurationGeneration = 2;
    const auto newGeneration = DescribeGuideHistory(
        frame, MotionSource::NvidiaOpticalFlow);
    assert(histories.Acquire(
        view, 17, newGeneration).resetRequired);

    frame.motionVectors.resolution = {960, 540};
    const auto resizedMotion = DescribeGuideHistory(
        frame, MotionSource::NvidiaOpticalFlow);
    assert(histories.Acquire(
        view, 18, resizedMotion).resetRequired);
    return 0;
}
