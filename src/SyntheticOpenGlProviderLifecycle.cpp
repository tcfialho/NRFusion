#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <bit>

namespace nrfusion {
namespace {

template <typename To, typename From>
To ProcCast(From value) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    return std::bit_cast<To>(value);
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

    gl_.CreateMemoryObjectsEXT = ProcCast<PFN_glCreateMemoryObjectsEXT_>(
        gl_.wglGetProcAddress("glCreateMemoryObjectsEXT"));
    gl_.DeleteMemoryObjectsEXT = ProcCast<PFN_glDeleteMemoryObjectsEXT_>(
        gl_.wglGetProcAddress("glDeleteMemoryObjectsEXT"));
    gl_.MemoryObjectParameterivEXT = ProcCast<PFN_glMemoryObjectParameterivEXT_>(
        gl_.wglGetProcAddress("glMemoryObjectParameterivEXT"));
    gl_.TexStorageMem2DEXT = ProcCast<PFN_glTexStorageMem2DEXT_>(
        gl_.wglGetProcAddress("glTexStorageMem2DEXT"));
    gl_.ImportMemoryWin32HandleEXT =
        ProcCast<PFN_glImportMemoryWin32HandleEXT_>(
            gl_.wglGetProcAddress("glImportMemoryWin32HandleEXT"));
    gl_.GenSemaphoresEXT = ProcCast<PFN_glGenSemaphoresEXT_>(
        gl_.wglGetProcAddress("glGenSemaphoresEXT"));
    gl_.DeleteSemaphoresEXT = ProcCast<PFN_glDeleteSemaphoresEXT_>(
        gl_.wglGetProcAddress("glDeleteSemaphoresEXT"));
    gl_.ImportSemaphoreWin32HandleEXT =
        ProcCast<PFN_glImportSemaphoreWin32HandleEXT_>(
            gl_.wglGetProcAddress("glImportSemaphoreWin32HandleEXT"));
    gl_.WaitSemaphoreEXT = ProcCast<PFN_glWaitSemaphoreEXT_>(
        gl_.wglGetProcAddress("glWaitSemaphoreEXT"));
    gl_.SignalSemaphoreEXT = ProcCast<PFN_glSignalSemaphoreEXT_>(
        gl_.wglGetProcAddress("glSignalSemaphoreEXT"));
    gl_.CopyImageSubData = ProcCast<PFN_glCopyImageSubData_>(
        gl_.wglGetProcAddress("glCopyImageSubData"));
    gl_.GetStringi = ProcCast<PFN_glGetStringi_>(
        gl_.wglGetProcAddress("glGetStringi"));

    gl_.hasInterop =
        gl_.CreateMemoryObjectsEXT &&
        gl_.DeleteMemoryObjectsEXT &&
        gl_.MemoryObjectParameterivEXT &&
        gl_.TexStorageMem2DEXT &&
        gl_.ImportMemoryWin32HandleEXT &&
        gl_.GenSemaphoresEXT &&
        gl_.DeleteSemaphoresEXT &&
        gl_.ImportSemaphoreWin32HandleEXT &&
        gl_.WaitSemaphoreEXT &&
        gl_.SignalSemaphoreEXT &&
        gl_.CopyImageSubData;
    return true;
}

bool SyntheticOpenGlProvider::CreatePrivateD3D12() {
    if (d3d12Device_) return true;

    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device_));
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC qDesc{};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(d3d12Device_->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&d3d12Queue_)))) {
        return false;
    }

    for (auto& s : sharedSlots_) {
        if (FAILED(d3d12Device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s.alloc)))) {
            return false;
        }
    }

    if (FAILED(d3d12Device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, sharedSlots_[0].alloc.Get(), nullptr, IID_PPV_ARGS(&d3d12CmdList_)))) {
        return false;
    }
    d3d12CmdList_->Close();

    if (FAILED(d3d12Device_->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d3d12Fence_)))) {
        return false;
    }

    if (FAILED(d3d12Device_->CreateSharedHandle(d3d12Fence_.Get(), nullptr, GENERIC_ALL, nullptr, &d3d12FenceSharedHandle_))) {
        return false;
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
        return false;
    }

    ready_ = true;
    return true;
}

void SyntheticOpenGlProvider::Shutdown() {
    std::scoped_lock lock(mutex_);
    if (!ready_) return;

    CloseSharedHandles();
    for (auto& s : sharedSlots_) {
        s.alloc.Reset();
    }

    nvof_.Shutdown();
    syntheticD3D12_.Shutdown();

    if (d3d12FenceSharedHandle_) {
        CloseHandle(d3d12FenceSharedHandle_);
        d3d12FenceSharedHandle_ = nullptr;
    }

    d3d12Fence_.Reset();
    d3d12CmdList_.Reset();
    d3d12Queue_.Reset();
    d3d12Device_.Reset();

    ready_ = false;
}

} // namespace nrfusion
