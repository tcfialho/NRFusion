#include "OpenGlExternalInteropHarness.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>

namespace nrfusion::test {
namespace {

constexpr wchar_t kWindowClass[] =
    L"NRFusionOpenGlExternalInteropHarness";

LRESULT CALLBACK WindowProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

template <typename To>
To LoadGlProc(const char* name) noexcept {
    PROC proc = wglGetProcAddress(name);
    if (!proc) return nullptr;
    const auto raw = std::bit_cast<std::uintptr_t>(proc);
    if (raw <= 3 ||
        raw == std::numeric_limits<std::uintptr_t>::max()) {
        return nullptr;
    }
    return std::bit_cast<To>(proc);
}

bool SameLuid(const LUID& left, const LUID& right) noexcept {
    return left.LowPart == right.LowPart &&
           left.HighPart == right.HighPart;
}

} // namespace

bool OpenGlExternalInteropHarness::CreateContext() {
    WNDCLASSW windowClass{};
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassW(&windowClass) != 0) {
        classRegistered_ = true;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    window_ = CreateWindowExW(
        0, kWindowClass, L"NRFusion OpenGL Interop",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 64, 64,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window_) return false;

    dc_ = GetDC(window_);
    if (!dc_) return false;

    PIXELFORMATDESCRIPTOR pixelFormat{};
    pixelFormat.nSize = sizeof(pixelFormat);
    pixelFormat.nVersion = 1;
    pixelFormat.dwFlags =
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pixelFormat.iPixelType = PFD_TYPE_RGBA;
    pixelFormat.cColorBits = 32;
    pixelFormat.cAlphaBits = 8;
    pixelFormat.cDepthBits = 24;
    pixelFormat.iLayerType = PFD_MAIN_PLANE;

    const int selected = ChoosePixelFormat(dc_, &pixelFormat);
    if (selected == 0 ||
        !SetPixelFormat(dc_, selected, &pixelFormat)) {
        return false;
    }

    context_ = wglCreateContext(dc_);
    return context_ != nullptr &&
           wglMakeCurrent(dc_, context_) == TRUE;
}

bool OpenGlExternalInteropHarness::LoadInterop() {
    gl_.CreateMemoryObjectsEXT =
        LoadGlProc<PFN_glCreateMemoryObjectsEXT_>(
            "glCreateMemoryObjectsEXT");
    gl_.DeleteMemoryObjectsEXT =
        LoadGlProc<PFN_glDeleteMemoryObjectsEXT_>(
            "glDeleteMemoryObjectsEXT");
    gl_.MemoryObjectParameterivEXT =
        LoadGlProc<PFN_glMemoryObjectParameterivEXT_>(
            "glMemoryObjectParameterivEXT");
    gl_.TexStorageMem2DEXT =
        LoadGlProc<PFN_glTexStorageMem2DEXT_>(
            "glTexStorageMem2DEXT");
    gl_.ImportMemoryWin32HandleEXT =
        LoadGlProc<PFN_glImportMemoryWin32HandleEXT_>(
            "glImportMemoryWin32HandleEXT");
    gl_.GenSemaphoresEXT =
        LoadGlProc<PFN_glGenSemaphoresEXT_>(
            "glGenSemaphoresEXT");
    gl_.DeleteSemaphoresEXT =
        LoadGlProc<PFN_glDeleteSemaphoresEXT_>(
            "glDeleteSemaphoresEXT");
    gl_.ImportSemaphoreWin32HandleEXT =
        LoadGlProc<PFN_glImportSemaphoreWin32HandleEXT_>(
            "glImportSemaphoreWin32HandleEXT");
    gl_.SemaphoreParameterui64vEXT =
        LoadGlProc<PFN_glSemaphoreParameterui64vEXT_>(
            "glSemaphoreParameterui64vEXT");
    gl_.WaitSemaphoreEXT =
        LoadGlProc<PFN_glWaitSemaphoreEXT_>(
            "glWaitSemaphoreEXT");
    gl_.SignalSemaphoreEXT =
        LoadGlProc<PFN_glSignalSemaphoreEXT_>(
            "glSignalSemaphoreEXT");
    gl_.CopyImageSubData =
        LoadGlProc<PFN_glCopyImageSubData_>(
            "glCopyImageSubData");
    gl_.GetUnsignedBytevEXT =
        LoadGlProc<PFN_glGetUnsignedBytevEXT_>(
            "glGetUnsignedBytevEXT");

    return gl_.CreateMemoryObjectsEXT &&
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
}

bool OpenGlExternalInteropHarness::ProbeProvider() {
    SyntheticOpenGlProvider provider;
    ProviderContext providerContext{};
    providerContext.api = GraphicsApi::OpenGL;
    if (!provider.Initialize(providerContext)) return false;
    provider.Shutdown();
    return true;
}

bool OpenGlExternalInteropHarness::CreateD3D12() {
    std::array<GLubyte, GL_LUID_SIZE_EXT> luidBytes{};
    while (glGetError() != GL_NO_ERROR) {}
    gl_.GetUnsignedBytevEXT(
        GL_DEVICE_LUID_EXT, luidBytes.data());
    if (glGetError() != GL_NO_ERROR) return false;

    LUID glLuid{};
    static_assert(sizeof(glLuid) == GL_LUID_SIZE_EXT);
    std::memcpy(&glLuid, luidBytes.data(), sizeof(glLuid));

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumerated =
            factory->EnumAdapters1(index, &candidate);
        if (enumerated == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumerated)) return false;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(candidate->GetDesc1(&desc)) &&
            SameLuid(desc.AdapterLuid, glLuid)) {
            adapter = candidate;
            break;
        }
    }
    if (!adapter ||
        FAILED(D3D12CreateDevice(
            adapter.Get(), D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device_)))) {
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device_->CreateCommandQueue(
            &queueDesc, IID_PPV_ARGS(&queue_))) ||
        FAILED(device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator_))) ||
        FAILED(device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator_.Get(), nullptr,
            IID_PPV_ARGS(&commandList_)))) {
        return false;
    }
    return SUCCEEDED(commandList_->Close());
}

bool OpenGlExternalInteropHarness::Open() {
    if (!CreateContext() ||
        !LoadInterop() ||
        !ProbeProvider() ||
        !CreateD3D12()) {
        Close();
        return false;
    }
    return true;
}

void OpenGlExternalInteropHarness::Close() noexcept {
    commandList_.Reset();
    allocator_.Reset();
    queue_.Reset();
    device_.Reset();

    if (context_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(context_);
        context_ = nullptr;
    }
    if (dc_ && window_) {
        ReleaseDC(window_, dc_);
        dc_ = nullptr;
    }
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (classRegistered_) {
        UnregisterClassW(
            kWindowClass, GetModuleHandleW(nullptr));
        classRegistered_ = false;
    }
    gl_ = {};
}

} // namespace nrfusion::test
