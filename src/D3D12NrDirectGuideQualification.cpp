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
        if (device == nullptr) return false;
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 adapterDesc{};
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(), IID_PPV_ARGS(&adapter))) ||
            FAILED(adapter->GetDesc1(&adapterDesc)) ||
            adapterDesc.VendorId != 0x10de)
            return false;

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
            options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_1)
            return false;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupportDepth{ DXGI_FORMAT_R32_FLOAT };
        const bool depthSupport = SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &formatSupportDepth, sizeof(formatSupportDepth))) &&
            ((formatSupportDepth.Support1 & (D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_LOAD)) != 0);

        D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupportMotion{ DXGI_FORMAT_R16G16_FLOAT };
        const bool motionSupport = SUCCEEDED(device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &formatSupportMotion, sizeof(formatSupportMotion))) &&
            ((formatSupportMotion.Support1 & (D3D12_FORMAT_SUPPORT1_TEXTURE2D | D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_LOAD)) != 0);

        const bool graphicsQualified = (GetModuleHandleW(L"nvwgf2umx.dll") != nullptr);
        const bool ngxQualified = (GetModuleHandleW(L"_nvngx.dll") != nullptr ||
                                   GetModuleHandleW(L"nvngx.dll") != nullptr);

        const bool qualified = depthSupport && motionSupport && (graphicsQualified || ngxQualified);
        NRF_LOG_INFO("NRGuides", "Typeless direct guides: %s (GPU=%04x graphics=%d NGX=%d)",
                     qualified ? "qualified" : "clone fallback", adapterDesc.DeviceId,
                     graphicsQualified, ngxQualified);
        return qualified;
    } catch (...) {
        return false;
    }
}

} // namespace nrfusion
