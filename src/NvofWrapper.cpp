#include "nrfusion/NvofWrapper.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace nrfusion {

NvofWrapper::NvofWrapper() {
#ifdef _WIN32
    HMODULE hMod = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hMod) {
        hMod = LoadLibraryW(L"nvofapi64.dll");
    }
    if (hMod) {
        moduleHandle_ = hMod;
        dllPath_ = "C:\\Windows\\System32\\nvofapi64.dll";

        auto createD3D12 = GetProcAddress(hMod, "NvOFAPICreateInstanceD3D12");
        if (createD3D12 != nullptr) {
            available_ = true;
            maxApiVersion_ = 0x00040000; // NVOF API 4.x
        } else {
            FreeLibrary(hMod);
            moduleHandle_ = nullptr;
        }
    }
#endif
}

NvofWrapper::~NvofWrapper() {
#ifdef _WIN32
    if (moduleHandle_) {
        FreeLibrary(static_cast<HMODULE>(moduleHandle_));
        moduleHandle_ = nullptr;
    }
#endif
}

NvofResolutionPlan NvofWrapper::Plan(std::uint32_t width, std::uint32_t height,
                                    NvofResolution requested) const {
    return policy_.Resolve(width, height, requested);
}

} // namespace nrfusion
