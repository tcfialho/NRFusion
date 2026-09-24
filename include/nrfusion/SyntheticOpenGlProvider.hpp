#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <gl/GL.h>
#include <wrl/client.h>

#include "nrfusion/SyntheticProvider.hpp"
#include "nrfusion/SyntheticDx12Provider.hpp"
#include "nrfusion/MotionVectorResolver.hpp"
#include "nrfusion/NvofMotionProvider.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace nrfusion {

using Microsoft::WRL::ComPtr;

// OpenGL External Memory & Semaphore Tokens (GL_EXT_memory_object_win32 / GL_EXT_semaphore_win32)
#ifndef GL_HANDLE_TYPE_D3D12_RESOURCE_EXT
#define GL_HANDLE_TYPE_D3D12_RESOURCE_EXT    0x958A
#endif
#ifndef GL_HANDLE_TYPE_D3D12_FENCE_EXT
#define GL_HANDLE_TYPE_D3D12_FENCE_EXT       0x9594
#endif
#ifndef GL_DEDICATED_MEMORY_OBJECT_EXT
#define GL_DEDICATED_MEMORY_OBJECT_EXT       0x9581
#endif
#ifndef GL_TEXTURE_TILING_EXT
#define GL_TEXTURE_TILING_EXT                0x9580
#endif
#ifndef GL_LAYOUT_COLOR_ATTACHMENT_EXT
#define GL_LAYOUT_COLOR_ATTACHMENT_EXT       0x958E
#endif
#ifndef GL_LAYOUT_SHADER_READ_ONLY_EXT
#define GL_LAYOUT_SHADER_READ_ONLY_EXT       0x9591
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F                           0x881A
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER                       0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER                  0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER                  0x8CA9
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0                 0x8CE0
#endif

// Function pointer definitions for dynamic WGL / OpenGL resolution
typedef void (APIENTRY *PFN_glCreateMemoryObjectsEXT_)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glDeleteMemoryObjectsEXT_)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_glMemoryObjectParameterivEXT_)(GLuint, GLenum, const GLint*);
typedef void (APIENTRY *PFN_glTexStorageMem2DEXT_)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, uint64_t);
typedef void (APIENTRY *PFN_glImportMemoryWin32HandleEXT_)(GLuint, uint64_t, GLenum, void*);

typedef void (APIENTRY *PFN_glGenSemaphoresEXT_)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glDeleteSemaphoresEXT_)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_glImportSemaphoreWin32HandleEXT_)(GLuint, GLenum, void*);
typedef void (APIENTRY *PFN_glWaitSemaphoreEXT_)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
typedef void (APIENTRY *PFN_glSignalSemaphoreEXT_)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);

typedef void (APIENTRY *PFN_glGenFramebuffers_)(GLsizei, GLuint*);
typedef void (APIENTRY *PFN_glDeleteFramebuffers_)(GLsizei, const GLuint*);
typedef void (APIENTRY *PFN_glBindFramebuffer_)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTexture2D_)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (APIENTRY *PFN_glBlitFramebuffer_)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (APIENTRY *PFN_glCopyImageSubData_)(GLuint, GLenum, GLint, GLint, GLint, GLint,
                                                 GLuint, GLenum, GLint, GLint, GLint, GLint,
                                                 GLsizei, GLsizei, GLsizei);
typedef const GLubyte* (APIENTRY *PFN_glGetStringi_)(GLenum, GLuint);

#ifndef GL_NUM_EXTENSIONS
#define GL_NUM_EXTENSIONS 0x821D
#endif

struct OpenGlDispatchTable {
    HMODULE libGl = nullptr;
    PROC (WINAPI *wglGetProcAddress)(LPCSTR) = nullptr;
    HGLRC (WINAPI *wglGetCurrentContext)(void) = nullptr;

    PFN_glCreateMemoryObjectsEXT_ CreateMemoryObjectsEXT = nullptr;
    PFN_glDeleteMemoryObjectsEXT_ DeleteMemoryObjectsEXT = nullptr;
    PFN_glMemoryObjectParameterivEXT_ MemoryObjectParameterivEXT = nullptr;
    PFN_glTexStorageMem2DEXT_ TexStorageMem2DEXT = nullptr;
    PFN_glImportMemoryWin32HandleEXT_ ImportMemoryWin32HandleEXT = nullptr;

    PFN_glGenSemaphoresEXT_ GenSemaphoresEXT = nullptr;
    PFN_glDeleteSemaphoresEXT_ DeleteSemaphoresEXT = nullptr;
    PFN_glImportSemaphoreWin32HandleEXT_ ImportSemaphoreWin32HandleEXT = nullptr;
    PFN_glWaitSemaphoreEXT_ WaitSemaphoreEXT = nullptr;
    PFN_glSignalSemaphoreEXT_ SignalSemaphoreEXT = nullptr;

    PFN_glGenFramebuffers_ GenFramebuffers = nullptr;
    PFN_glDeleteFramebuffers_ DeleteFramebuffers = nullptr;
    PFN_glBindFramebuffer_ BindFramebuffer = nullptr;
    PFN_glFramebufferTexture2D_ FramebufferTexture2D = nullptr;
    PFN_glBlitFramebuffer_ BlitFramebuffer = nullptr;
    PFN_glCopyImageSubData_ CopyImageSubData = nullptr;
    PFN_glGetStringi_ GetStringi = nullptr;

    bool isLoaded = false;
    bool hasInterop = false;
};

class SyntheticOpenGlProvider : public ISyntheticProvider {
public:
    static constexpr uint32_t kMaxInFlight = 3;

    SyntheticOpenGlProvider();
    ~SyntheticOpenGlProvider() override;

    bool Initialize(const ProviderContext& context) override;
    void Shutdown() override;
    bool IsReady() const noexcept override { return ready_; }

    SyntheticWorkHandle Submit(const SyntheticFrameInputs& inputs, void* commandList) override;
    bool Poll(const SyntheticWorkHandle& handle) override;
    ResourceRef GetResidual(const SyntheticWorkHandle& handle) override;
    bool ComposeNative(const SyntheticWorkHandle& handle,
                       const ResourceRef& originalNative,
                       const ResourceRef& destinationNative,
                       void* commandList,
                       float residualWeight = 1.0f) override;

    const char* Name() const noexcept override { return "SyntheticOpenGlProvider"; }

    // Dynamic OpenGL loader and interop query
    bool LoadOpenGl();
    bool HasOpenGlInterop() const noexcept { return gl_.hasInterop; }

    // OpenGL side GPU copy: game texture into D3D12-shared texture
    bool RecordOpenGlInputCopy(GLuint gameColorTex, uint32_t width, uint32_t height);

    // OpenGL side GPU consume: compose imported residual back into game buffer
    bool RecordOpenGlOutputConsume(GLuint gameDestTex, uint32_t width, uint32_t height);

    ID3D12Device* PrivateD3D12Device() const noexcept { return d3d12Device_.Get(); }
    NvofMotionProvider& OpticalFlow() noexcept { return nvof_; }

private:
    struct SharedSlot {
        uint64_t workId = 0;
        uint64_t producerFenceValue = 0;

        // D3D12 private side
        ComPtr<ID3D12Resource> d3d12Color;
        ComPtr<ID3D12Resource> d3d12Residual;
        ComPtr<ID3D12CommandAllocator> alloc;
        HANDLE colorSharedHandle = nullptr;
        HANDLE residualSharedHandle = nullptr;

        // OpenGL import side
        GLuint glColorMem = 0;
        GLuint glColorTex = 0;
        GLuint glResidualMem = 0;
        GLuint glResidualTex = 0;
        GLuint glInputSem = 0;
        GLuint glOutputSem = 0;

        bool inUse = false;
    };

    bool CreatePrivateD3D12();
    bool CreateSharedResources(uint32_t width, uint32_t height);
    void CloseSharedHandles();

    OpenGlDispatchTable gl_{};

    ComPtr<ID3D12Device> d3d12Device_;
    ComPtr<ID3D12CommandQueue> d3d12Queue_;
    ComPtr<ID3D12GraphicsCommandList> d3d12CmdList_;
    ComPtr<ID3D12Fence> d3d12Fence_;
    HANDLE d3d12FenceSharedHandle_ = nullptr;
    uint64_t nextFenceValue_ = 1;

    SyntheticDx12Provider syntheticD3D12_;
    NvofMotionProvider nvof_;

    Resolution currentRes_{};
    std::array<SharedSlot, kMaxInFlight> sharedSlots_{};
    uint32_t currentSlot_ = 0;
    bool ready_ = false;
    mutable std::mutex mutex_;
};

} // namespace nrfusion
