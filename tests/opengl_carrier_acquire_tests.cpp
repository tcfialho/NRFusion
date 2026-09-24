#include "nrfusion/OpenGlCarrierAcquire.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

OpenGlNativeTextureFacts Texture(std::uint32_t id) {
    OpenGlNativeTextureFacts facts{};
    facts.textureId = id;
    facts.resolution = {1920, 1080};
    facts.format = ResourceFormat::Rgba16Float;
    facts.target = OpenGlTextureTarget::Texture2D;
    facts.mipLevels = 1;
    facts.sampleCount = 1;
    return facts;
}

OpenGlNativeAcquireInput Base() {
    OpenGlNativeAcquireInput input{};
    input.identity.frameId = 42;
    input.identity.viewId = 7;
    input.identity.configurationGeneration = 9;
    input.resourceGeneration = 3;
    input.interop.renderContext = 0x1234;
    input.interop.memoryObject = true;
    input.interop.memoryObjectWin32 = true;
    input.interop.semaphore = true;
    input.interop.semaphoreWin32 = true;
    input.interop.copyImage = true;
    input.color.texture = Texture(11);
    input.color.provenance = ResourceProvenance::GameNative;
    input.color.reliability = ResourceReliability::Reliable;
    input.output = Texture(12);
    return input;
}

} // namespace

int main() {
    const auto base = Base();
    const auto acquired = AcquireOpenGlFrame(base);
    assert(acquired);
    assert(acquired.frame.api == GraphicsApi::OpenGL);
    assert(acquired.frame.color.opaqueId == 11);
    assert(acquired.resourceGeneration == 3);
    assert(acquired.interop.Ready());

    auto missingContext = base;
    missingContext.interop.renderContext = 0;
    assert(AcquireOpenGlFrame(missingContext).failure ==
           OpenGlAcquireFailure::MissingContext);

    auto missingExtension = base;
    missingExtension.interop.semaphoreWin32 = false;
    assert(AcquireOpenGlFrame(missingExtension).failure ==
           OpenGlAcquireFailure::MissingInteropCapability);

    auto staleGeneration = base;
    staleGeneration.resourceGeneration = 0;
    assert(AcquireOpenGlFrame(staleGeneration).failure ==
           OpenGlAcquireFailure::InvalidResourceGeneration);

    auto wrongFormat = base;
    wrongFormat.color.texture.format = ResourceFormat::Rgba8Unorm;
    assert(AcquireOpenGlFrame(wrongFormat).failure ==
           OpenGlAcquireFailure::InvalidColor);

    auto mismatchedOutput = base;
    mismatchedOutput.output.resolution = {1280, 720};
    assert(AcquireOpenGlFrame(mismatchedOutput).failure ==
           OpenGlAcquireFailure::ResolutionMismatch);

    WorkTicket ticket{};
    ticket.id = 1;
    ticket.session = 2;
    ticket.sourceFrame = acquired.frame.frameId;
    ticket.viewKey = acquired.frame.viewId;
    ticket.configurationGeneration =
        acquired.frame.configurationGeneration;
    ticket.workingScale = 1.0f;
    const auto work = BuildOpenGlCarrierWork(acquired.frame, ticket);
    assert(work && work->color.opaqueId == 11);

    auto wrongTicket = ticket;
    ++wrongTicket.configurationGeneration;
    assert(!BuildOpenGlCarrierWork(acquired.frame, wrongTicket));
    return 0;
}
