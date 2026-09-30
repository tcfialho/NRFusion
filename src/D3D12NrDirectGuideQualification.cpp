#include "nrfusion/D3D12NrExecutor.hpp"
#include "nrfusion/Logger.hpp"
#include "nrfusion/Sha256.hpp"

#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

namespace nrfusion {
namespace {

std::wstring ModulePath(HMODULE module) {
    if (module == nullptr) return {};
    wchar_t path[32768]{};
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    return count != 0 && count < 32768 ? std::wstring(path, count) : std::wstring{};
}

bool HasVersion(const std::wstring& path, DWORD high, DWORD low) {
    if (path.empty()) return false;
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0) return false;
    std::vector<unsigned char> version(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, version.data())) return false;
    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixedSize = 0;
    return VerQueryValueW(version.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) &&
        fixed != nullptr && fixedSize >= sizeof(*fixed) && fixed->dwSignature == 0xfeef04bd &&
        fixed->dwFileVersionMS == high && fixed->dwFileVersionLS == low;
}

} // namespace

bool D3D12NrExecutor::QualifyDirectGuides(ID3D12Device* device) noexcept {
    try {
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 adapterDesc{};
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) ||
            FAILED(adapter->GetDesc1(&adapterDesc)) ||
            adapterDesc.VendorId != 0x10de || adapterDesc.DeviceId != 0x28a1)
            return false;

        constexpr DWORD driverHigh = (32u << 16);
        constexpr DWORD driverLow = (16u << 16) | 1714u;
        WIN32_FILE_ATTRIBUTE_DATA snippetAttributes{};
        const bool graphicsQualified =
            HasVersion(ModulePath(GetModuleHandleW(L"nvwgf2umx.dll")), driverHigh, driverLow);
        const bool ngxQualified = HasVersion(ModulePath(GetModuleHandleW(L"_nvngx.dll")),
                                              (31u << 16), (15u << 16) | 4619u);
        const bool qualified =
            graphicsQualified && ngxQualified &&
            HasVersion(ModulePath(driverModule_), (30u << 16), (14u << 16) | 9516u) &&
            HasVersion(snippetPath_, (310u << 16) | 8u, 0) &&
            GetFileAttributesExW(snippetPath_.c_str(), GetFileExInfoStandard, &snippetAttributes) &&
            snippetAttributes.nFileSizeHigh == 0 && snippetAttributes.nFileSizeLow == 165840496u &&
            Sha256FileEquals(ModulePath(forwarderModule_),
                "25ffade884f50cad0174030335fd6216bbd569cb415bc35c958224a8c96f3c4e");
        NRF_LOG_INFO("NRGuides", "Typeless direct guides: %s (GPU=%04x graphics=%d NGX=%d)",
                     qualified ? "qualified" : "clone fallback", adapterDesc.DeviceId,
                     graphicsQualified, ngxQualified);
        return qualified;
    } catch (...) {
        return false;
    }
}

} // namespace nrfusion
