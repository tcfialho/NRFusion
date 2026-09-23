#include "nrfusion/D3D12CarrierExecutionPlan.hpp"

#include <cassert>
#include <limits>

using namespace nrfusion;

namespace {

ResourceRef Resource(
    std::uint64_t id, FrameId frameId, Resolution resolution,
    ResourceFormat format) {
    ResourceRef resource{};
    resource.opaqueId = id;
    resource.resolution = resolution;
    resource.format = format;
    resource.provenance = ResourceProvenance::GameNative;
    resource.reliability = ResourceReliability::Reliable;
    resource.ownership = ResourceOwnership::Borrowed;
    resource.lifetime = ResourceLifetime::Frame;
    resource.sourceFrameId = frameId;
    return resource;
}

D3D12CarrierFrameResult Frame(NrPlacement placement) {
    D3D12CarrierFrameResult frame{};
    frame.acquire.attempted = true;
    frame.acquire.frame.frameId = 77;
    frame.acquire.frame.configurationGeneration = 5;
    frame.acquire.frame.api = GraphicsApi::D3D12;
    frame.acquire.frame.renderResolution = {1920, 1080};
    frame.acquire.frame.outputResolution = {3840, 2160};
    frame.acquire.frame.hdr = true;
    frame.acquire.outputOpaqueId = 4;
    frame.acquire.frame.color = Resource(
        1, 77, {1920, 1080}, ResourceFormat::Rgba16Float);
    frame.acquire.frame.depth = Resource(
        2, 77, {1920, 1080}, ResourceFormat::D32Float);
    frame.acquire.frame.motionVectors = Resource(
        3, 77, {960, 540}, ResourceFormat::Rg16Float);

    frame.session.disposition = NrSessionDisposition::Ready;
    frame.session.frameId = 77;
    frame.session.configurationGeneration = 5;
    frame.session.runtimeGeneration = 9;
    frame.session.decision.supported = true;
    frame.session.decision.pipeline.supported = true;
    frame.session.decision.pipeline.api = GraphicsApi::D3D12;
    frame.session.decision.pipeline.placement = placement;
    frame.session.decision.workingScale = 0.75f;
    return frame;
}

D3D12CarrierWork Work() {
    D3D12CarrierWork work{};
    work.ticket.id = 21;
    work.ticket.session = 2;
    work.ticket.sourceFrame = 77;
    work.ticket.configurationGeneration = 9;
    work.ticket.workingScale = 0.75f;
    work.submissionEpoch = work.ticket.id;
    return work;
}

} // namespace

int main() {
    D3D12CarrierExecutionConfig config{};
    config.passes = 2;
    config.motionScaleX = 2.0f;
    config.motionScaleY = 2.0f;

    const auto pre = BuildD3D12CarrierExecutionPlan(
        Frame(NrPlacement::PreSr), Work(), config);
    assert(pre);
    assert(pre.plan.framePlan.beforeUpscale);
    const Resolution render{1920, 1080};
    assert(pre.plan.framePlan.colorSurface == render);
    assert(pre.plan.framePlan.activeColor.width == 1920);
    assert(pre.plan.framePlan.activeColor.height == 1080);
    const Resolution preWork{1440, 810};
    assert(pre.plan.resolvedPlan.work == preWork);
    assert(pre.plan.resolvedPlan.requestedPasses == 2);
    assert(pre.plan.submissionEpoch == Work().ticket.id);
    assert(pre.plan.colourIsLinearHdr);
    assert(pre.plan.motionScaleX == 2.0f);
    assert(pre.plan.motionScaleY == 2.0f);

    const auto post = BuildD3D12CarrierExecutionPlan(
        Frame(NrPlacement::PostSr), Work(), {});
    assert(post);
    assert(!post.plan.framePlan.beforeUpscale);
    const Resolution output{3840, 2160};
    const Resolution postWork{2880, 1620};
    assert(post.plan.framePlan.colorSurface == output);
    assert(post.plan.resolvedPlan.work == postWork);

    auto resetFrame = Frame(NrPlacement::PreSr);
    resetFrame.acquire.frame.cameraCut = true;
    assert(BuildD3D12CarrierExecutionPlan(resetFrame, Work(), {}).plan.reset);

    auto missingDepth = Frame(NrPlacement::PreSr);
    missingDepth.acquire.frame.depth = {};
    assert(BuildD3D12CarrierExecutionPlan(missingDepth, Work(), {}).failure ==
           D3D12CarrierExecutionFailure::MissingDepth);

    auto missingMotion = Frame(NrPlacement::PreSr);
    missingMotion.acquire.frame.motionVectors = {};
    assert(BuildD3D12CarrierExecutionPlan(missingMotion, Work(), {}).failure ==
           D3D12CarrierExecutionFailure::MissingMotion);

    auto exposureConfig = config;
    exposureConfig.useGameExposure = true;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), Work(), exposureConfig).failure ==
           D3D12CarrierExecutionFailure::MissingExposure);

    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::AcrossRr), Work(), {}).failure ==
           D3D12CarrierExecutionFailure::StageMismatch);

    auto storeConfig = config;
    storeConfig.stage = D3D12CarrierExecutionStage::AcrossRrStore;
    const auto store = BuildD3D12CarrierExecutionPlan(
        Frame(NrPlacement::AcrossRr), Work(), storeConfig);
    assert(store);
    assert(store.plan.modelStage);
    assert(store.plan.requiresColor);
    assert(store.plan.framePlan.beforeUpscale);
    assert(store.plan.runBeforeUpscale);
    assert(store.plan.rayReconstruction);
    assert(store.plan.residualAcrossRr);
    assert(store.plan.submissionEpoch == Work().submissionEpoch);

    auto applyFrame = Frame(NrPlacement::AcrossRr);
    applyFrame.acquire.frame.depth = {};
    applyFrame.acquire.frame.motionVectors = {};
    auto applyConfig = config;
    applyConfig.stage = D3D12CarrierExecutionStage::AcrossRrApply;
    applyConfig.useGameExposure = true;
    applyConfig.motionScaleX =
        std::numeric_limits<float>::quiet_NaN();
    const auto apply = BuildD3D12CarrierExecutionPlan(
        applyFrame, Work(), applyConfig);
    assert(apply);
    assert(!apply.plan.modelStage);
    assert(!apply.plan.requiresColor);
    assert(!apply.plan.framePlan.beforeUpscale);
    assert(apply.plan.runBeforeUpscale);
    assert(apply.plan.rayReconstruction);
    assert(apply.plan.residualAcrossRr);
    assert(apply.plan.submissionEpoch == store.plan.submissionEpoch);

    auto wrongStoreStage = storeConfig;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PostSr), Work(), wrongStoreStage).failure ==
           D3D12CarrierExecutionFailure::StageMismatch);

    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::DeferredResidual), Work(), {}).failure ==
           D3D12CarrierExecutionFailure::UnsupportedPlacement);

    auto wrongFrameWork = Work();
    wrongFrameWork.ticket.sourceFrame = 76;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), wrongFrameWork, {}).failure ==
           D3D12CarrierExecutionFailure::WorkFrameMismatch);

    auto staleWork = Work();
    staleWork.ticket.configurationGeneration = 8;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), staleWork, {}).failure ==
           D3D12CarrierExecutionFailure::WorkGenerationMismatch);

    auto badEpoch = Work();
    ++badEpoch.submissionEpoch;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), badEpoch, {}).failure ==
           D3D12CarrierExecutionFailure::InvalidWork);

    auto badMotion = config;
    badMotion.motionScaleX = std::numeric_limits<float>::quiet_NaN();
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), Work(), badMotion).failure ==
           D3D12CarrierExecutionFailure::InvalidMotionScale);

    auto manyPasses = config;
    manyPasses.passes = 99;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), Work(), manyPasses)
               .plan.resolvedPlan.requestedPasses == 3);

    manyPasses.unlockPasses = true;
    assert(BuildD3D12CarrierExecutionPlan(
               Frame(NrPlacement::PreSr), Work(), manyPasses)
               .plan.resolvedPlan.requestedPasses == 30);
    return 0;
}
