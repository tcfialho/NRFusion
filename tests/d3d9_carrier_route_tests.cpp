#include "nrfusion/D3D9CarrierRoute.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

D3D9CarrierRouteFacts ValidExFacts() {
    D3D9CarrierRouteFacts facts{};
    facts.variant = D3D9CarrierVariant::Ex;
    facts.d3d9ExAvailable = true;
    facts.wddm = true;
    facts.sameAdapter = true;
    facts.colorTexture2D = true;
    facts.colorFormat = ResourceFormat::Rgba16Float;
    facts.defaultPool = true;
    facts.singleMip = true;
    facts.noMsaa = true;
    facts.sharedHandle = true;
    facts.d3d11OpenSharedResource = true;
    facts.manualSurfaceQueue = true;
    facts.dequeueTimeoutZero = true;
    facts.enqueueDoNotWait = true;
    facts.flushDoNotWait = true;
    facts.queueDepth = 2;
    facts.d3d11NtSharedSurface = true;
    facts.d3d12OpenNtHandle = true;
    facts.gpuCopyInbound = true;
    facts.gpuCopyOutbound = true;
    facts.composeBackGpu = true;
    facts.resetOwnershipDefined = true;
    return facts;
}

} // namespace

int main() {
    D3D9CarrierRouteFacts classic{};
    classic.variant = D3D9CarrierVariant::Classic;
    assert(QualifyD3D9CarrierRoute(classic).failure ==
           D3D9CarrierRouteFailure::ClassicNoGpuSharedSurface);

    const auto valid = QualifyD3D9CarrierRoute(ValidExFacts());
    assert(valid);
    assert(valid.plan.inboundFullFrameCopies == 2);
    assert(valid.plan.outboundFullFrameCopies == 2);
    assert(valid.plan.usesManualSurfaceQueue);
    assert(valid.plan.nonBlockingBackpressure);
    assert(valid.plan.colorOnly);

    auto facts = ValidExFacts();
    facts.d3d9ExAvailable = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::MissingD3D9Ex);

    facts = ValidExFacts();
    facts.wddm = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::MissingWddm);

    facts = ValidExFacts();
    facts.sameAdapter = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::AdapterMismatch);

    facts = ValidExFacts();
    facts.colorFormat = ResourceFormat::Rgba8Unorm;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::UnsupportedFormat);

    facts = ValidExFacts();
    facts.defaultPool = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::InvalidSharedTexture);

    facts = ValidExFacts();
    facts.manualSurfaceQueue = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::MissingManualSyncQueue);

    facts = ValidExFacts();
    facts.enqueueDoNotWait = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::BlockingSyncPolicy);

    facts = ValidExFacts();
    facts.queueDepth = 1;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::InsufficientQueueDepth);

    facts = ValidExFacts();
    facts.d3d12OpenNtHandle = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::MissingD3D12NtImport);

    facts = ValidExFacts();
    facts.composeBackGpu = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::MissingComposeBack);

    facts = ValidExFacts();
    facts.resetOwnershipDefined = false;
    assert(QualifyD3D9CarrierRoute(facts).failure ==
           D3D9CarrierRouteFailure::ResetOwnershipUndefined);
    return 0;
}
