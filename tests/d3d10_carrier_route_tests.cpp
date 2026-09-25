#include "nrfusion/D3D10CarrierRoute.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

D3D10BridgeRouteFacts ValidFacts() {
    D3D10BridgeRouteFacts facts{};
    facts.d3d10_1 = true;
    facts.sameAdapter = true;
    facts.sourceTexture2D = true;
    facts.sourceFormat = ResourceFormat::Rgba16Float;
    facts.legacySharedSurface = true;
    facts.keyedMutex = true;
    facts.zeroTimeoutAcquire = true;
    facts.d3d11OpenLegacy = true;
    facts.d3d11NtSharedSurface = true;
    facts.d3d12OpenNtHandle = true;
    facts.gpuCopyInbound = true;
    facts.gpuCopyOutbound = true;
    facts.composeBackGpu = true;
    return facts;
}

} // namespace

int main() {
    const auto valid = QualifyD3D10BridgeRoute(ValidFacts());
    assert(valid);
    assert(valid.plan.inboundFullFrameCopies == 2);
    assert(valid.plan.outboundFullFrameCopies == 2);
    assert(valid.plan.usesLegacyDxgiHandle);
    assert(valid.plan.usesNtHandleAfterD3D11);
    assert(valid.plan.usesZeroTimeoutKeyedMutex);

    auto facts = ValidFacts();
    facts.d3d10_1 = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingD3D10_1);

    facts = ValidFacts();
    facts.sameAdapter = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::AdapterMismatch);

    facts = ValidFacts();
    facts.sourceFormat = ResourceFormat::Rgba8Unorm;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::UnsupportedFormat);

    facts = ValidFacts();
    facts.legacySharedSurface = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingLegacySharedSurface);

    facts = ValidFacts();
    facts.keyedMutex = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingKeyedMutex);

    facts = ValidFacts();
    facts.zeroTimeoutAcquire = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::BlockingMutexPolicy);

    facts = ValidFacts();
    facts.d3d11OpenLegacy = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingD3D11LegacyOpen);

    facts = ValidFacts();
    facts.d3d11NtSharedSurface = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingD3D11NtShare);

    facts = ValidFacts();
    facts.d3d12OpenNtHandle = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingD3D12NtImport);

    facts = ValidFacts();
    facts.gpuCopyInbound = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingGpuCopy);

    facts = ValidFacts();
    facts.composeBackGpu = false;
    assert(QualifyD3D10BridgeRoute(facts).failure ==
           D3D10BridgeRouteFailure::MissingComposeBack);
    return 0;
}
