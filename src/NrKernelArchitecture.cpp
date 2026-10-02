#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <cstring>
#include "NrKernelArchitecture.hpp"

namespace nrfusion::kernelprofile {
bool SupportsSm89Device(ID3D12Device* device) {
    const auto driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!driver) return false;
    using Init = int(__stdcall*)(unsigned);
    using Count = int(__stdcall*)(int*);
    using Luid = int(__stdcall*)(char*, unsigned*, int);
    using Attribute = int(__stdcall*)(int*, int, int);
    const auto init = reinterpret_cast<Init>(GetProcAddress(driver, "cuInit"));
    const auto count = reinterpret_cast<Count>(GetProcAddress(driver, "cuDeviceGetCount"));
    const auto luid = reinterpret_cast<Luid>(GetProcAddress(driver, "cuDeviceGetLuid"));
    const auto attribute = reinterpret_cast<Attribute>(GetProcAddress(driver, "cuDeviceGetAttribute"));
    int devices = 0;
    bool supported = false;
    if (init && count && luid && attribute && init(0) == 0 && count(&devices) == 0) {
        const auto adapter = device->GetAdapterLuid();
        for (int index = 0; index < devices; ++index) {
            char candidate[sizeof(LUID)]{};
            unsigned mask = 0;
            int major = 0, minor = 0;
            if (luid(candidate, &mask, index) == 0 && !std::memcmp(candidate, &adapter, sizeof(adapter)) &&
                attribute(&major, 75, index) == 0 && attribute(&minor, 76, index) == 0)
                supported = major == 8 && minor == 9;
        }
    }
    FreeLibrary(driver);
    return supported;
}

}
