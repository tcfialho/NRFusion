#include "OpenGlExternalInteropHarness.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nrfusion::test {
namespace {

void CopyForIdentityModel(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* input,
    ID3D12Resource* output) {
    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = input;
    barriers[0].Transition.StateBefore =
        D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter =
        D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type =
        D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = output;
    barriers[1].Transition.StateBefore =
        D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter =
        D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(2, barriers);
    commandList->CopyResource(output, input);
    std::swap(
        barriers[0].Transition.StateBefore,
        barriers[0].Transition.StateAfter);
    std::swap(
        barriers[1].Transition.StateBefore,
        barriers[1].Transition.StateAfter);
    commandList->ResourceBarrier(2, barriers);
}

bool VerifyTexture(
    GLuint texture,
    const std::vector<float>& expected) {
    std::vector<float> actual(expected.size(), 0.0f);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(
        GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
        actual.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) return false;
    return std::equal(
        expected.begin(), expected.end(), actual.begin(),
        [](float left, float right) {
            return std::fabs(left - right) < 0.003f;
        });
}

} // namespace

bool OpenGlExternalInteropHarness::RunProviderCycles(
    std::uint32_t count) {
    SyntheticOpenGlProvider provider;
    ProviderContext context{};
    context.api = GraphicsApi::OpenGL;
    if (!provider.Initialize(context)) return false;

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    GLuint source = 0;
    GLuint destination = 0;
    glGenTextures(1, &source);
    glGenTextures(1, &destination);

    bool ok = source != 0 && destination != 0;
    for (std::uint32_t cycle = 0;
         cycle != count && ok; ++cycle) {
        const std::uint32_t width =
            32 + (cycle % 3) * 16;
        const std::uint32_t height =
            32 + (cycle % 2) * 16;
        const std::size_t pixels =
            static_cast<std::size_t>(width) * height;
        std::vector<float> expected(pixels * 4);
        const float base =
            static_cast<float>((cycle % 5) + 1) / 6.0f;
        for (std::size_t pixel = 0;
             pixel != pixels; ++pixel) {
            expected[pixel * 4 + 0] = base;
            expected[pixel * 4 + 1] = 1.0f - base;
            expected[pixel * 4 + 2] = base * 0.25f;
            expected[pixel * 4 + 3] = 1.0f;
        }

        glBindTexture(GL_TEXTURE_2D, source);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA16F,
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height),
            0, GL_RGBA, GL_FLOAT, expected.data());
        glBindTexture(GL_TEXTURE_2D, destination);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA16F,
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height),
            0, GL_RGBA, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (glGetError() != GL_NO_ERROR) {
            ok = false;
            break;
        }

        SyntheticFrameInputs inputs{};
        inputs.ticket.id = cycle + 1;
        inputs.ticket.session = 1;
        inputs.frameId = cycle + 1;
        inputs.renderResolution = {width, height};
        inputs.targetResolution = {width, height};
        inputs.workingScale = 1.0f;
        inputs.color.opaqueId = source;
        inputs.color.resolution = {width, height};
        inputs.color.format = ResourceFormat::Rgba16Float;

        const auto handle = provider.Submit(inputs, nullptr);
        const auto view = provider.GetD3D12Work(handle);
        if (!handle.valid || !view || !view->Valid()) {
            ok = false;
            break;
        }

        if (!allocator) {
            if (FAILED(view->device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&allocator))) ||
                FAILED(view->device->CreateCommandList(
                    0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    allocator.Get(), nullptr,
                    IID_PPV_ARGS(&commandList)))) {
                ok = false;
                break;
            }
        } else if (FAILED(allocator->Reset()) ||
                   FAILED(commandList->Reset(
                       allocator.Get(), nullptr))) {
            ok = false;
            break;
        }

        CopyForIdentityModel(
            commandList.Get(),
            view->inputColor,
            view->neuralOutput);
        if (FAILED(commandList->Close())) {
            ok = false;
            break;
        }
        ID3D12CommandList* lists[] = {commandList.Get()};
        view->queue->ExecuteCommandLists(1, lists);

        ok = provider.PublishD3D12Result(handle) &&
             provider.RecordOpenGlOutputConsume(
                 handle, destination, width, height) &&
             VerifyTexture(destination, expected);
    }

    if (source) glDeleteTextures(1, &source);
    if (destination) glDeleteTextures(1, &destination);
    provider.Shutdown();
    return ok;
}

} // namespace nrfusion::test
