#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "nrfusion/D3D11CarrierNativeAcquire.hpp"
#include "nrfusion/D3D11CarrierWork.hpp"
#include "nrfusion/MotionVectorResolver.hpp"
#include "nrfusion/NvofMotionProvider.hpp"
#include "nrfusion/SyntheticDx11BridgeProvider.hpp"

#include <cassert>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;
using namespace nrfusion;

namespace {

ComPtr<ID3D11Texture2D> MakeTexture(
    ID3D11Device* device, UINT width, UINT height) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;
    assert(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)));
    return texture;
}

bool WaitForD3D11(ID3D11Device* device, ID3D11DeviceContext* context) {
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    if (FAILED(device->CreateQuery(&desc, &query))) return false;
    context->End(query.Get());
    context->Flush();
    for (int i = 0; i < 5000; ++i) {
        if (context->GetData(query.Get(), nullptr, 0, 0) == S_OK) return true;
        Sleep(1);
    }
    return false;
}

}

int main() {
    std::cout << "[Synthetic Dx11 Bridge Test] Starting Stage 2 validation..." << std::endl;

    // 1. Motion Vector Resolver Hierarchy Verification
    {
        Resolution fullRes{ 1920, 1080 };
        MotionCandidates c{};

        // Case 1: Native MV present and reliable
        c.nativeEngineMv.opaqueId = 1;
        c.nativeEngineMv.resolution = { 1920, 1080 };
        c.nativeEngineMv.format = ResourceFormat::Rg16Float;
        c.nativeReliable = true;
        c.nvofHardwareAvailable = true;

        auto r1 = MotionVectorResolver::Resolve(c, fullRes);
        assert(r1.category == ResolvedMotionCategory::NativeEngine);
        assert(r1.source == MotionSource::Native);
        assert(!r1.requiresNvofCompute);

        // Case 2: Native missing, DLSS contract present
        c.nativeReliable = false;
        c.dlssContractMv.opaqueId = 2;
        c.dlssContractMv.resolution = { 1920, 1080 };
        c.dlssContractMv.format = ResourceFormat::Rg16Float;
        c.contractReliable = true;

        auto r2 = MotionVectorResolver::Resolve(c, fullRes);
        assert(r2.category == ResolvedMotionCategory::DlssContract);
        assert(!r2.requiresNvofCompute);

        // Case 3: Contract missing, Shader estimated present
        c.contractReliable = false;
        c.shaderEstimatedMv.opaqueId = 3;
        c.shaderEstimatedMv.resolution = { 960, 540 };
        c.shaderEstimatedMv.format = ResourceFormat::Rg16Float;
        c.shaderReliable = true;

        auto r3 = MotionVectorResolver::Resolve(c, fullRes);
        assert(r3.category == ResolvedMotionCategory::ShaderEstimated);
        assert(r3.scaleX == 2.0f);
        assert(!r3.requiresNvofCompute);

        // Case 4: No engine vectors, NVOF available
        c.shaderReliable = false;
        auto r4 = MotionVectorResolver::Resolve(c, fullRes);
        assert(r4.category == ResolvedMotionCategory::NvidiaOpticalFlow);
        assert(r4.requiresNvofCompute);

        // Case 5: Camera cut
        c.cameraCut = true;
        auto r5 = MotionVectorResolver::Resolve(c, fullRes);
        assert(r5.category == ResolvedMotionCategory::ZeroFallback);
        assert(!r5.requiresNvofCompute);

        std::cout << "  [PASS] MotionVectorResolver strict 5-level hierarchy validated." << std::endl;
    }

    // 2. Hardware D3D11 Device Creation
    ComPtr<ID3D11Device> d3d11Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d11Device, &fl, &d3d11Context);
    if (FAILED(hr)) {
        // Fallback to WARP for test coverage if hardware device not granted
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d11Device, &fl, &d3d11Context);
    }
    if (FAILED(hr) || !d3d11Device) {
        std::cerr << "Failed to create D3D11 device" << std::endl;
        return 1;
    }

    // 3. SyntheticDx11BridgeProvider Initialization
    SyntheticDx11BridgeProvider bridge;
    ProviderContext ctx{};
    ctx.api = GraphicsApi::D3D11;
    ctx.device = d3d11Device.Get();
    ctx.is32Bit = false;

    bool initOk = bridge.Initialize(ctx);
    assert(initOk);
    assert(bridge.IsReady());
    assert(bridge.PrivateD3D12Device() != nullptr);
    std::cout << "  [PASS] SyntheticDx11BridgeProvider private D3D12 bridge initialized." << std::endl;

    // 4. Low-Res Asynchronous NVOF Validation (180p Height)
    {
        NvofMotionProvider& nvof = bridge.OpticalFlow();
        assert(nvof.IsReady());
        Resolution fRes = nvof.FlowResolution();
        assert(fRes.height == 180);
        assert(fRes.width == 320); // 1920 * 180 / 1080 = 320
        std::cout << "  [PASS] NvofMotionProvider 180p low-res geometry validated: " << fRes.width << "x" << fRes.height << std::endl;
    }

    // 5. GPU-only bridge handoff, retirement and resize/reset stress.
    {
        for (std::uint64_t frame = 1; frame <= 12; ++frame) {
            const UINT width = (frame & 1u) ? 640u : 800u;
            const UINT height = (frame & 1u) ? 360u : 450u;
            auto gameColor = MakeTexture(d3d11Device.Get(), width, height);
            auto gameDest = MakeTexture(d3d11Device.Get(), width, height);
            D3D11NativeAcquireInput acquire{};
            acquire.identity.frameId = 2000 + frame;
            acquire.identity.configurationGeneration = 1;
            acquire.context = d3d11Context.Get();
            acquire.color = gameColor.Get();
            const auto acquired = AcquireD3D11NativeFrame(acquire);
            assert(acquired);

            WorkTicket ticket{};
            ticket.id = 2000 + frame;
            ticket.session = 1;
            ticket.sourceFrame = acquired.frame.frameId;
            ticket.configurationGeneration = 1;
            ticket.workingScale = 0.75f;
            const auto work = BuildD3D11CarrierWork(acquired.frame, ticket);
            assert(work);

            const SyntheticWorkHandle handle = bridge.Submit(*work, nullptr);
            assert(handle.valid);
            assert(handle.workId == work->ticket.id);
            assert(bridge.RecordD3D11OutputConsume(
                handle, d3d11Context.Get(), gameDest.Get()));
            assert(WaitForD3D11(d3d11Device.Get(), d3d11Context.Get()));
            assert(bridge.Poll(handle));
        }
        std::cout << "  [PASS] GPU-fenced handoff, slot retirement and resize stress confirmed."
                  << std::endl;
    }

    bridge.Shutdown();
    assert(!bridge.IsReady());
    assert(bridge.Initialize(ctx));
    assert(bridge.IsReady());
    bridge.Shutdown();
    assert(!bridge.IsReady());

    std::cout << "[Synthetic Dx11 Bridge Test] All Stage 2 assertions PASSED." << std::endl;
    return 0;
}
