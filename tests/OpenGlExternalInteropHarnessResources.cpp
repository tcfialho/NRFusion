#include "OpenGlExternalInteropHarness.hpp"

#include <algorithm>
#include <cmath>

namespace nrfusion::test {
namespace {

bool GlOkay() noexcept {
    return glGetError() == GL_NO_ERROR;
}

} // namespace

bool OpenGlExternalInteropHarness::CreateResources(
    std::uint32_t width,
    std::uint32_t height,
    OpenGlInteropResources& resources) {
    if (!device_ || width == 0 || height == 0) return false;
    resources.width = width;
    resources.height = height;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (FAILED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&resources.input))) ||
        FAILED(device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_SHARED, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&resources.output))) ||
        FAILED(device_->CreateFence(
            0, D3D12_FENCE_FLAG_SHARED,
            IID_PPV_ARGS(&resources.fence))) ||
        FAILED(device_->CreateSharedHandle(
            resources.input.Get(), nullptr, GENERIC_ALL,
            nullptr, &resources.inputHandle)) ||
        FAILED(device_->CreateSharedHandle(
            resources.output.Get(), nullptr, GENERIC_ALL,
            nullptr, &resources.outputHandle)) ||
        FAILED(device_->CreateSharedHandle(
            resources.fence.Get(), nullptr, GENERIC_ALL,
            nullptr, &resources.fenceHandle))) {
        DestroyResources(resources);
        return false;
    }

    gl_.CreateMemoryObjectsEXT(1, &resources.inputMemory);
    gl_.CreateMemoryObjectsEXT(1, &resources.outputMemory);
    const GLint dedicated = GL_TRUE;
    gl_.MemoryObjectParameterivEXT(
        resources.inputMemory,
        GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    gl_.MemoryObjectParameterivEXT(
        resources.outputMemory,
        GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
    gl_.ImportMemoryWin32HandleEXT(
        resources.inputMemory, 0,
        GL_HANDLE_TYPE_D3D12_RESOURCE_EXT,
        resources.inputHandle);
    gl_.ImportMemoryWin32HandleEXT(
        resources.outputMemory, 0,
        GL_HANDLE_TYPE_D3D12_RESOURCE_EXT,
        resources.outputHandle);

    glGenTextures(1, &resources.inputTexture);
    glBindTexture(GL_TEXTURE_2D, resources.inputTexture);
    gl_.TexStorageMem2DEXT(
        GL_TEXTURE_2D, 1, GL_RGBA16F,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        resources.inputMemory, 0);
    glGenTextures(1, &resources.outputTexture);
    glBindTexture(GL_TEXTURE_2D, resources.outputTexture);
    gl_.TexStorageMem2DEXT(
        GL_TEXTURE_2D, 1, GL_RGBA16F,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        resources.outputMemory, 0);

    glGenTextures(1, &resources.gameSource);
    glBindTexture(GL_TEXTURE_2D, resources.gameSource);
    glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA16F,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0, GL_RGBA, GL_FLOAT, nullptr);

    glGenTextures(1, &resources.gameDestination);
    glBindTexture(GL_TEXTURE_2D, resources.gameDestination);
    glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(
        GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA16F,
        static_cast<GLsizei>(width),
        static_cast<GLsizei>(height),
        0, GL_RGBA, GL_FLOAT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    gl_.GenSemaphoresEXT(1, &resources.inputSemaphore);
    gl_.GenSemaphoresEXT(1, &resources.outputSemaphore);
    gl_.ImportSemaphoreWin32HandleEXT(
        resources.inputSemaphore,
        GL_HANDLE_TYPE_D3D12_FENCE_EXT,
        resources.fenceHandle);
    gl_.ImportSemaphoreWin32HandleEXT(
        resources.outputSemaphore,
        GL_HANDLE_TYPE_D3D12_FENCE_EXT,
        resources.fenceHandle);

    if (!resources.inputMemory ||
        !resources.outputMemory ||
        !resources.inputTexture ||
        !resources.outputTexture ||
        !resources.inputSemaphore ||
        !resources.outputSemaphore ||
        !resources.gameSource ||
        !resources.gameDestination ||
        !GlOkay()) {
        DestroyResources(resources);
        return false;
    }
    return true;
}

void OpenGlExternalInteropHarness::DestroyResources(
    OpenGlInteropResources& resources) noexcept {
    if (resources.gameSource)
        glDeleteTextures(1, &resources.gameSource);
    if (resources.gameDestination)
        glDeleteTextures(1, &resources.gameDestination);
    if (resources.inputTexture)
        glDeleteTextures(1, &resources.inputTexture);
    if (resources.outputTexture)
        glDeleteTextures(1, &resources.outputTexture);
    if (resources.inputMemory && gl_.DeleteMemoryObjectsEXT)
        gl_.DeleteMemoryObjectsEXT(1, &resources.inputMemory);
    if (resources.outputMemory && gl_.DeleteMemoryObjectsEXT)
        gl_.DeleteMemoryObjectsEXT(1, &resources.outputMemory);
    if (resources.inputSemaphore && gl_.DeleteSemaphoresEXT)
        gl_.DeleteSemaphoresEXT(1, &resources.inputSemaphore);
    if (resources.outputSemaphore && gl_.DeleteSemaphoresEXT)
        gl_.DeleteSemaphoresEXT(1, &resources.outputSemaphore);

    if (resources.inputHandle)
        CloseHandle(resources.inputHandle);
    if (resources.outputHandle)
        CloseHandle(resources.outputHandle);
    if (resources.fenceHandle)
        CloseHandle(resources.fenceHandle);
    resources = {};
}

bool OpenGlExternalInteropHarness::WaitForFence(
    ID3D12Fence* fence,
    std::uint64_t value) const noexcept {
    if (!fence) return false;
    if (fence->GetCompletedValue() >= value) return true;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    const bool armed =
        SUCCEEDED(fence->SetEventOnCompletion(value, event));
    const bool completed =
        armed && WaitForSingleObject(event, 5000) == WAIT_OBJECT_0;
    CloseHandle(event);
    return completed;
}

bool OpenGlExternalInteropHarness::VerifyDestination(
    const OpenGlInteropResources& resources,
    const std::vector<float>& expected) const {
    std::vector<float> actual(expected.size(), 0.0f);
    glBindTexture(
        GL_TEXTURE_2D, resources.gameDestination);
    glGetTexImage(
        GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT,
        actual.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!GlOkay()) return false;

    return std::equal(
        expected.begin(), expected.end(), actual.begin(),
        [](float left, float right) {
            return std::fabs(left - right) < 0.0025f;
        });
}

} // namespace nrfusion::test
