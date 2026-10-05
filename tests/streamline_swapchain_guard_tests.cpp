#include "nrfusion/StreamlineSwapchainGuard.hpp"
#include <MinHook.h>
#include <sl_core_types.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

HMODULE CreateFakeCommonModule() {
    constexpr std::size_t imageSize = 0x2000;
    auto* image = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, imageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!image) return nullptr;
    std::memset(image, 0xCC, imageSize);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.NumberOfSections = 1;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(imageSize);
    auto* section = IMAGE_FIRST_SECTION(nt);
    section->VirtualAddress = 0x1000;
    section->Misc.VirtualSize = 0x500;
    section->Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;

    // Write signature: 48 89 5C 24 08 57 48 83 EC 60 48 8B 02
    static const uint8_t kSignature[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x02,
        // Function epilogue: xor eax, eax; add rsp, 0x60; pop rdi; ret
        0x48, 0x31, 0xC0, 0x48, 0x83, 0xC4, 0x60, 0x5F, 0xC3
    };
    std::memcpy(image + section->VirtualAddress + 0x100, kSignature, sizeof(kSignature));
    return reinterpret_cast<HMODULE>(image);
}

void FreeFakeCommonModule(HMODULE mod) {
    if (mod) VirtualFree(mod, 0, MEM_RELEASE);
}

} // namespace

int main() {
    auto& guard = nrfusion::StreamlineSwapchainGuard::Instance();
    guard.Reset();
    assert(!guard.IsDlssgGuarded());
    assert(!guard.IsCommonGuarded());

    // 1. Install with null target does not crash
    guard.Install(nullptr);

    // 2. Test common guard on synthetic module
    HMODULE fakeCommon = CreateFakeCommonModule();
    assert(fakeCommon != nullptr);

    bool commonInstalled = guard.Install(fakeCommon);
    assert(commonInstalled);
    assert(guard.IsCommonGuarded());

    // Verify buffer function was hooked and catches null swapchain
    using CommonFn = HRESULT(*)(void*, IDXGISwapChain*, uint32_t, void**);
    auto* targetFunc = reinterpret_cast<CommonFn>(
        reinterpret_cast<std::uint8_t*>(fakeCommon) + 0x1100);

    // Calling with null swapchain must return DXGI_ERROR_INVALID_CALL (0x887A0001) without crashing
    void* resultPtr = nullptr;
    HRESULT hr = targetFunc(nullptr, nullptr, 0, &resultPtr);
    assert(hr == DXGI_ERROR_INVALID_CALL);

    // Calling with null res pointer must also return DXGI_ERROR_INVALID_CALL
    hr = targetFunc(nullptr, reinterpret_cast<IDXGISwapChain*>(0x1234), 0, nullptr);
    assert(hr == DXGI_ERROR_INVALID_CALL);

    // 3. Test real sl.dlss_g.dll if available on disk
    HMODULE realDlssg = LoadLibraryW(L"D:\\Games\\Resident Evil Requiem\\sl.dlss_g.dll");
    if (realDlssg) {
        bool dlssgInstalled = guard.Install(realDlssg);
        assert(dlssgInstalled);
        assert(guard.IsDlssgGuarded());

        using GetPluginFn = void*(*)(const char*);
        auto getFn = reinterpret_cast<GetPluginFn>(GetProcAddress(realDlssg, "slGetPluginFunction"));
        assert(getFn != nullptr);

        using CreateSwapChainForHwndFn = sl::Result(*)(
            void*, void*, HWND, const void*, const void*, void*, IDXGISwapChain1**, bool*);
        auto hookFn = reinterpret_cast<CreateSwapChainForHwndFn>(getFn("slHookCreateSwapChainForHwnd"));
        assert(hookFn != nullptr);

        // Test with null swapChain double pointer: must return eOk and not crash
        bool handled = true;
        sl::Result slRes = hookFn(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &handled);
        assert(slRes == sl::Result::eOk);
        assert(!handled);

        // Test with swapChain pointer pointing to null: must return eOk and not crash
        IDXGISwapChain1* nullChain = nullptr;
        handled = true;
        slRes = hookFn(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &nullChain, &handled);
        assert(slRes == sl::Result::eOk);
        assert(!handled);

        FreeLibrary(realDlssg);
    }

    // 4. Reset cleans up hooks
    guard.Reset();
    assert(!guard.IsDlssgGuarded());
    assert(!guard.IsCommonGuarded());

    FreeFakeCommonModule(fakeCommon);
    std::cout << "All streamline swapchain guard tests passed successfully!\n";
    return 0;
}
