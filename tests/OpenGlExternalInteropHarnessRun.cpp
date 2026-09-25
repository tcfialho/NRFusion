#include "OpenGlExternalInteropHarness.hpp"

#include <limits>

namespace nrfusion::test {
namespace {

void Transition(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

bool GlOkay() noexcept {
    return glGetError() == GL_NO_ERROR;
}

} // namespace

bool OpenGlExternalInteropHarness::ExecuteCycle(
    OpenGlInteropResources& resources,
    std::uint32_t seed) {
    constexpr std::uint64_t maxValue =
        std::numeric_limits<std::uint64_t>::max();
    if (resources.nextFenceValue == 0 ||
        resources.nextFenceValue > maxValue - 2) {
        return false;
    }

    const std::size_t pixels =
        static_cast<std::size_t>(resources.width) *
        resources.height;
    std::vector<float> expected(pixels * 4);
    const float base =
        static_cast<float>((seed % 7) + 1) / 8.0f;
    for (std::size_t pixel = 0; pixel != pixels; ++pixel) {
        expected[pixel * 4 + 0] = base;
        expected[pixel * 4 + 1] = base * 0.5f;
        expected[pixel * 4 + 2] = 1.0f - base;
        expected[pixel * 4 + 3] = 1.0f;
    }

    glBindTexture(GL_TEXTURE_2D, resources.gameSource);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0,
        static_cast<GLsizei>(resources.width),
        static_cast<GLsizei>(resources.height),
        GL_RGBA, GL_FLOAT, expected.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    const std::uint64_t inputValue =
        resources.nextFenceValue++;
    const std::uint64_t outputValue =
        resources.nextFenceValue++;
    const std::uint64_t releaseValue =
        resources.nextFenceValue++;
    const GLenum layout = GL_LAYOUT_GENERAL_EXT;

    gl_.SemaphoreParameterui64vEXT(
        resources.inputSemaphore,
        GL_D3D12_FENCE_VALUE_EXT, &inputValue);
    gl_.CopyImageSubData(
        resources.gameSource, GL_TEXTURE_2D, 0, 0, 0, 0,
        resources.inputTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(resources.width),
        static_cast<GLsizei>(resources.height), 1);
    gl_.SignalSemaphoreEXT(
        resources.inputSemaphore, 0, nullptr,
        1, &resources.inputTexture, &layout);
    if (!GlOkay() ||
        FAILED(queue_->Wait(
            resources.fence.Get(), inputValue)) ||
        FAILED(allocator_->Reset()) ||
        FAILED(commandList_->Reset(
            allocator_.Get(), nullptr))) {
        return false;
    }

    Transition(
        commandList_.Get(), resources.input.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(
        commandList_.Get(), resources.output.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    commandList_->CopyResource(
        resources.output.Get(), resources.input.Get());
    Transition(
        commandList_.Get(), resources.output.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_COMMON);
    Transition(
        commandList_.Get(), resources.input.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_COMMON);
    if (FAILED(commandList_->Close())) return false;

    ID3D12CommandList* lists[] = {commandList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    if (FAILED(queue_->Signal(
            resources.fence.Get(), outputValue))) {
        return false;
    }

    gl_.SemaphoreParameterui64vEXT(
        resources.outputSemaphore,
        GL_D3D12_FENCE_VALUE_EXT, &outputValue);
    gl_.WaitSemaphoreEXT(
        resources.outputSemaphore, 0, nullptr,
        1, &resources.outputTexture, &layout);
    gl_.CopyImageSubData(
        resources.outputTexture, GL_TEXTURE_2D, 0, 0, 0, 0,
        resources.gameDestination, GL_TEXTURE_2D,
        0, 0, 0, 0,
        static_cast<GLsizei>(resources.width),
        static_cast<GLsizei>(resources.height), 1);
    gl_.SemaphoreParameterui64vEXT(
        resources.inputSemaphore,
        GL_D3D12_FENCE_VALUE_EXT, &releaseValue);
    const GLuint textures[] = {
        resources.inputTexture, resources.outputTexture};
    const GLenum layouts[] = {
        GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT};
    gl_.SignalSemaphoreEXT(
        resources.inputSemaphore, 0, nullptr,
        2, textures, layouts);
    if (!GlOkay() ||
        !WaitForFence(resources.fence.Get(), releaseValue)) {
        return false;
    }
    return VerifyDestination(resources, expected);
}

bool OpenGlExternalInteropHarness::RunRecreationCycles(
    std::uint32_t count) {
    for (std::uint32_t cycle = 0; cycle != count; ++cycle) {
        OpenGlInteropResources resources{};
        const std::uint32_t width =
            32 + (cycle % 3) * 16;
        const std::uint32_t height =
            32 + (cycle % 2) * 16;
        const bool ok =
            CreateResources(width, height, resources) &&
            ExecuteCycle(resources, cycle + 1);
        DestroyResources(resources);
        if (!ok) return false;
    }
    return true;
}

bool OpenGlExternalInteropHarness::RunReuseCycles(
    std::uint32_t count) {
    OpenGlInteropResources resources{};
    if (!CreateResources(64, 64, resources))
        return false;
    bool ok = true;
    for (std::uint32_t cycle = 0;
         cycle != count && ok; ++cycle) {
        ok = ExecuteCycle(resources, 1000 + cycle);
    }
    DestroyResources(resources);
    return ok;
}

} // namespace nrfusion::test
