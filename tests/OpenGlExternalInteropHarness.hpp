#pragma once

#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <cstdint>
#include <vector>

namespace nrfusion::test {

struct OpenGlInteropResources {
    ComPtr<ID3D12Resource> input;
    ComPtr<ID3D12Resource> output;
    ComPtr<ID3D12Fence> fence;
    HANDLE inputHandle = nullptr;
    HANDLE outputHandle = nullptr;
    HANDLE fenceHandle = nullptr;
    GLuint inputMemory = 0;
    GLuint outputMemory = 0;
    GLuint inputTexture = 0;
    GLuint outputTexture = 0;
    GLuint inputSemaphore = 0;
    GLuint outputSemaphore = 0;
    GLuint gameSource = 0;
    GLuint gameDestination = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t nextFenceValue = 1;
};

class OpenGlExternalInteropHarness {
public:
    bool Open();
    void Close() noexcept;

    bool RunRecreationCycles(std::uint32_t count);
    bool RunReuseCycles(std::uint32_t count);
    bool RunProviderCycles(std::uint32_t count);

private:
    bool CreateContext();
    bool LoadInterop();
    bool CreateD3D12();
    bool ProbeProvider();

    bool CreateResources(
        std::uint32_t width,
        std::uint32_t height,
        OpenGlInteropResources& resources);
    void DestroyResources(
        OpenGlInteropResources& resources) noexcept;
    bool ExecuteCycle(
        OpenGlInteropResources& resources,
        std::uint32_t seed);
    bool WaitForFence(
        ID3D12Fence* fence,
        std::uint64_t value) const noexcept;
    bool VerifyDestination(
        const OpenGlInteropResources& resources,
        const std::vector<float>& expected) const;

    HWND window_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC context_ = nullptr;
    bool classRegistered_ = false;
    OpenGlDispatchTable gl_{};

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12CommandAllocator> allocator_;
    ComPtr<ID3D12GraphicsCommandList> commandList_;
};

} // namespace nrfusion::test
