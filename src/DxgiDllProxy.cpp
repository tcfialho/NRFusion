#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>

extern "C" void NRFusion_EnsureRuntime();
namespace nrfusion { void AttachProxyDxgiFactory(void* factory); }

namespace {
HMODULE RealDxgiModule() {
    static HMODULE module = [] {
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) return static_cast<HMODULE>(nullptr);
        const std::wstring path = std::wstring(directory, length) + L"\\dxgi.dll";
        return LoadLibraryW(path.c_str());
    }();
    return module;
}

template <typename Function>
Function ResolveDxgiExport(const char* name) {
    NRFusion_EnsureRuntime();
    const HMODULE module = RealDxgiModule();
    return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
}
}

extern "C" {
HRESULT WINAPI CreateDXGIFactory(REFIID iid, void** factory) {
    using Function = HRESULT(WINAPI*)(REFIID, void**);
    static const auto original = ResolveDxgiExport<Function>("CreateDXGIFactory");
    const HRESULT result = original ? original(iid, factory) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (SUCCEEDED(result) && factory && *factory) nrfusion::AttachProxyDxgiFactory(*factory);
    return result;
}

HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void** factory) {
    using Function = HRESULT(WINAPI*)(REFIID, void**);
    static const auto original = ResolveDxgiExport<Function>("CreateDXGIFactory1");
    const HRESULT result = original ? original(iid, factory) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (SUCCEEDED(result) && factory && *factory) nrfusion::AttachProxyDxgiFactory(*factory);
    return result;
}

HRESULT WINAPI CreateDXGIFactory2(UINT flags, REFIID iid, void** factory) {
    using Function = HRESULT(WINAPI*)(UINT, REFIID, void**);
    static const auto original = ResolveDxgiExport<Function>("CreateDXGIFactory2");
    const HRESULT result = original ? original(flags, iid, factory) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    if (SUCCEEDED(result) && factory && *factory) nrfusion::AttachProxyDxgiFactory(*factory);
    return result;
}

HRESULT WINAPI DXGIGetDebugInterface1(UINT flags, REFIID iid, void** debug) {
    using Function = HRESULT(WINAPI*)(UINT, REFIID, void**);
    static const auto original = ResolveDxgiExport<Function>("DXGIGetDebugInterface1");
    return original ? original(flags, iid, debug) : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    using Function = HRESULT(WINAPI*)();
    static const auto original = ResolveDxgiExport<Function>("DXGIDeclareAdapterRemovalSupport");
    return original ? original() : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}
}
