#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>

namespace nrfusion {
namespace {

template <typename To, typename From>
To ProcCast(From value) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    return std::bit_cast<To>(value);
}

template <typename To>
To WglProc(
    PROC (WINAPI *getProc)(LPCSTR),
    const char* name) noexcept {
    if (!getProc || !name) return nullptr;
    const PROC proc = getProc(name);
    if (!proc) return nullptr;
    const auto raw = std::bit_cast<std::uintptr_t>(proc);
    if (raw <= 3 ||
        raw == std::numeric_limits<std::uintptr_t>::max()) {
        return nullptr;
    }
    return std::bit_cast<To>(proc);
}

bool HasExtension(
    const OpenGlDispatchTable& gl,
    const char* expected) noexcept {
    if (!gl.GetStringi || !expected) return false;

    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    if (count <= 0) return false;

    for (GLint index = 0; index != count; ++index) {
        const auto* extension =
            gl.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(index));
        if (extension &&
            std::strcmp(
                reinterpret_cast<const char*>(extension),
                expected) == 0) {
            return true;
        }
    }
    return false;
}

bool HasRequiredInteropExtensions(
    const OpenGlDispatchTable& gl) noexcept {
    return HasExtension(gl, "GL_EXT_memory_object") &&
           HasExtension(gl, "GL_EXT_memory_object_win32") &&
           HasExtension(gl, "GL_EXT_semaphore") &&
           HasExtension(gl, "GL_EXT_semaphore_win32");
}

bool QueryGlAdapterLuid(
    const OpenGlDispatchTable& gl,
    LUID& luid) noexcept {
    if (!gl.GetUnsignedBytevEXT) return false;
    std::array<GLubyte, GL_LUID_SIZE_EXT> bytes{};
    while (glGetError() != GL_NO_ERROR) {}
    gl.GetUnsignedBytevEXT(GL_DEVICE_LUID_EXT, bytes.data());
    if (glGetError() != GL_NO_ERROR) return false;
    static_assert(sizeof(LUID) == GL_LUID_SIZE_EXT);
    std::memcpy(&luid, bytes.data(), sizeof(luid));
    return true;
}

bool SameLuid(const LUID& left, const LUID& right) noexcept {
    return left.LowPart == right.LowPart &&
           left.HighPart == right.HighPart;
}

bool FindDxgiAdapter(
    const LUID& luid,
    ComPtr<IDXGIAdapter1>& adapter) noexcept {
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumResult =
            factory->EnumAdapters1(index, &candidate);
        if (enumResult == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumResult)) return false;

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(candidate->GetDesc1(&desc)) &&
            SameLuid(desc.AdapterLuid, luid)) {
            adapter = candidate;
            return true;
        }
    }
    return false;
}

} // namespace

SyntheticOpenGlProvider::SyntheticOpenGlProvider() = default;

SyntheticOpenGlProvider::~SyntheticOpenGlProvider() {
    Shutdown();
}

bool SyntheticOpenGlProvider::LoadOpenGl() {
    if (!gl_.isLoaded) {
        gl_.libGl = GetModuleHandleW(L"opengl32.dll");
        if (!gl_.libGl) gl_.libGl = LoadLibraryW(L"opengl32.dll");
        if (!gl_.libGl) return false;

        gl_.wglGetProcAddress = ProcCast<PROC(WINAPI*)(LPCSTR)>(
            GetProcAddress(gl_.libGl, "wglGetProcAddress"));
        gl_.wglGetCurrentContext = ProcCast<HGLRC(WINAPI*)(void)>(
            GetProcAddress(gl_.libGl, "wglGetCurrentContext"));
        gl_.isLoaded = gl_.wglGetProcAddress && gl_.wglGetCurrentContext;
        if (!gl_.isLoaded) return false;
    }

    gl_.hasInterop = false;
    if (gl_.wglGetCurrentContext() == nullptr) return true;

    gl_.CreateMemoryObjectsEXT = WglProc<PFN_glCreateMemoryObjectsEXT_>(gl_.wglGetProcAddress, "glCreateMemoryObjectsEXT");
    gl_.DeleteMemoryObjectsEXT = WglProc<PFN_glDeleteMemoryObjectsEXT_>(gl_.wglGetProcAddress, "glDeleteMemoryObjectsEXT");
    gl_.MemoryObjectParameterivEXT = WglProc<PFN_glMemoryObjectParameterivEXT_>(gl_.wglGetProcAddress, "glMemoryObjectParameterivEXT");
    gl_.TexStorageMem2DEXT = WglProc<PFN_glTexStorageMem2DEXT_>(gl_.wglGetProcAddress, "glTexStorageMem2DEXT");
    gl_.ImportMemoryWin32HandleEXT =
        WglProc<PFN_glImportMemoryWin32HandleEXT_>(gl_.wglGetProcAddress, "glImportMemoryWin32HandleEXT");
    gl_.GenSemaphoresEXT = WglProc<PFN_glGenSemaphoresEXT_>(gl_.wglGetProcAddress, "glGenSemaphoresEXT");
    gl_.DeleteSemaphoresEXT = WglProc<PFN_glDeleteSemaphoresEXT_>(gl_.wglGetProcAddress, "glDeleteSemaphoresEXT");
    gl_.ImportSemaphoreWin32HandleEXT =
        WglProc<PFN_glImportSemaphoreWin32HandleEXT_>(gl_.wglGetProcAddress, "glImportSemaphoreWin32HandleEXT");
    gl_.SemaphoreParameterui64vEXT =
        WglProc<PFN_glSemaphoreParameterui64vEXT_>(gl_.wglGetProcAddress, "glSemaphoreParameterui64vEXT");
    gl_.WaitSemaphoreEXT = WglProc<PFN_glWaitSemaphoreEXT_>(gl_.wglGetProcAddress, "glWaitSemaphoreEXT");
    gl_.SignalSemaphoreEXT = WglProc<PFN_glSignalSemaphoreEXT_>(gl_.wglGetProcAddress, "glSignalSemaphoreEXT");
    gl_.CopyImageSubData = WglProc<PFN_glCopyImageSubData_>(gl_.wglGetProcAddress, "glCopyImageSubData");
    gl_.GetStringi = WglProc<PFN_glGetStringi_>(gl_.wglGetProcAddress, "glGetStringi");
    gl_.GetUnsignedBytevEXT =
        WglProc<PFN_glGetUnsignedBytevEXT_>(gl_.wglGetProcAddress, "glGetUnsignedBytevEXT");

    gl_.hasInterop =
        HasRequiredInteropExtensions(gl_) &&
        gl_.CreateMemoryObjectsEXT &&
        gl_.DeleteMemoryObjectsEXT &&
        gl_.MemoryObjectParameterivEXT &&
        gl_.TexStorageMem2DEXT &&
        gl_.ImportMemoryWin32HandleEXT &&
        gl_.GenSemaphoresEXT &&
        gl_.DeleteSemaphoresEXT &&
        gl_.ImportSemaphoreWin32HandleEXT &&
        gl_.SemaphoreParameterui64vEXT &&
        gl_.WaitSemaphoreEXT &&
        gl_.SignalSemaphoreEXT &&
        gl_.CopyImageSubData &&
        gl_.GetUnsignedBytevEXT;
    return true;
}

bool SyntheticOpenGlProvider::CreatePrivateD3D12() {
    if (d3d12Device_) return d3d12Queue_ && d3d12CmdList_;

    LUID glLuid{};
    ComPtr<IDXGIAdapter1> adapter;
    if (!QueryGlAdapterLuid(gl_, glLuid) ||
        !FindDxgiAdapter(glLuid, adapter))
        return false;

    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(
            adapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device))))
        return false;

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&queue))))
        return false;

    std::array<SharedSlot, kMaxInFlight> pending{};
    auto fail = [&pending]() noexcept {
        for (auto& slot : pending) {
            if (slot.fenceSharedHandle) CloseHandle(slot.fenceSharedHandle);
            slot.fenceSharedHandle = nullptr;
        }
        return false;
    };
    for (auto& slot : pending) {
        if (FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&slot.alloc))) ||
            FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&slot.publishAlloc))) ||
            FAILED(device->CreateFence(
                0, D3D12_FENCE_FLAG_SHARED,
                IID_PPV_ARGS(&slot.fence))) ||
            FAILED(device->CreateSharedHandle(
                slot.fence.Get(), nullptr, GENERIC_ALL, nullptr,
                &slot.fenceSharedHandle)))
            return fail();
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, pending[0].alloc.Get(),
            nullptr, IID_PPV_ARGS(&commandList))) ||
        FAILED(commandList->Close()))
        return fail();

    d3d12Device_ = std::move(device);
    d3d12Queue_ = std::move(queue);
    d3d12CmdList_ = std::move(commandList);
    for (std::uint32_t i = 0; i < kMaxInFlight; ++i) {
        sharedSlots_[i].alloc = std::move(pending[i].alloc);
        sharedSlots_[i].publishAlloc = std::move(pending[i].publishAlloc);
        sharedSlots_[i].fence = std::move(pending[i].fence);
        sharedSlots_[i].fenceSharedHandle = pending[i].fenceSharedHandle;
        pending[i].fenceSharedHandle = nullptr;
    }
    return true;
}

bool SyntheticOpenGlProvider::Initialize(const ProviderContext& context) {
    std::scoped_lock lock(mutex_);
    if (ready_) return true;
    if (context.api != GraphicsApi::OpenGL ||
        !LoadOpenGl() ||
        !gl_.wglGetCurrentContext ||
        gl_.wglGetCurrentContext() == nullptr ||
        !gl_.hasInterop) {
        return false;
    }

    if (!CreatePrivateD3D12()) {
        return false;
    }

    ProviderContext d12Ctx{};
    d12Ctx.api = GraphicsApi::D3D12;
    d12Ctx.device = d3d12Device_.Get();
    d12Ctx.commandQueue = d3d12Queue_.Get();
    d12Ctx.preferSameDevice = true;

    if (!syntheticD3D12_.Initialize(d12Ctx)) {
        ResetPrivateD3D12();
        return false;
    }

    ready_ = true;
    return true;
}

void SyntheticOpenGlProvider::Shutdown() {
    std::scoped_lock lock(mutex_);
    ResetPrivateD3D12();
    ready_ = false;
}

} // namespace nrfusion
