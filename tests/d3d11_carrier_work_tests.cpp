#include "nrfusion/D3D11CarrierWork.hpp"

#include <cassert>
#include <limits>

using namespace nrfusion;

namespace {

FrameContext Frame() {
    FrameContext frame{};
    frame.frameId = 10;
    frame.configurationGeneration = 3;
    frame.api = GraphicsApi::D3D11;
    frame.renderResolution = {1920, 1080};
    frame.outputResolution = {1920, 1080};
    frame.color.opaqueId = 100;
    frame.color.resolution = frame.renderResolution;
    frame.color.format = ResourceFormat::Rgba16Float;
    frame.color.provenance = ResourceProvenance::GameNative;
    frame.color.reliability = ResourceReliability::Reliable;
    frame.color.ownership = ResourceOwnership::Borrowed;
    frame.color.lifetime = ResourceLifetime::Frame;
    frame.color.sourceFrameId = frame.frameId;
    return frame;
}

WorkTicket Ticket() {
    WorkTicket ticket{};
    ticket.id = 11;
    ticket.session = 2;
    ticket.frameId = 10;
    ticket.configurationGeneration = 3;
    ticket.workingScale = 0.75f;
    return ticket;
}

} // namespace

int main() {
    const auto frame = Frame();
    const auto ticket = Ticket();
    const auto work = BuildD3D11CarrierWork(frame, ticket, 0.75f);
    assert(work);
    assert(work->ticket == ticket);
    assert(work->frameId == frame.frameId);
    assert(work->color.opaqueId == frame.color.opaqueId);
    assert(work->renderResolution == frame.renderResolution);
    assert(work->targetResolution == frame.outputResolution);
    assert(work->workingScale == 0.75f);

    auto wrongApi = frame;
    wrongApi.api = GraphicsApi::D3D12;
    assert(!BuildD3D11CarrierWork(wrongApi, ticket));

    auto staleGuide = frame;
    staleGuide.depth = frame.color;
    staleGuide.depth.sourceFrameId = frame.frameId - 1;
    assert(!BuildD3D11CarrierWork(staleGuide, ticket));

    auto unreliableGuide = frame;
    unreliableGuide.motionVectors = frame.color;
    unreliableGuide.motionVectors.reliability = ResourceReliability::Unreliable;
    assert(!BuildD3D11CarrierWork(unreliableGuide, ticket));

    auto wrongColor = frame;
    wrongColor.color.provenance = ResourceProvenance::Generated;
    assert(!BuildD3D11CarrierWork(wrongColor, ticket));

    auto unsupportedBridgeFormat = frame;
    unsupportedBridgeFormat.color.format = ResourceFormat::Rgba8Unorm;
    assert(!BuildD3D11CarrierWork(unsupportedBridgeFormat, ticket));

    auto invalidTicket = ticket;
    invalidTicket.id = 0;
    assert(!BuildD3D11CarrierWork(frame, invalidTicket));
    assert(!BuildD3D11CarrierWork(frame, ticket, 0.0f));
    assert(!BuildD3D11CarrierWork(frame, ticket, 1.1f));
    assert(!BuildD3D11CarrierWork(
        frame, ticket, (std::numeric_limits<float>::quiet_NaN)()));
    return 0;
}
