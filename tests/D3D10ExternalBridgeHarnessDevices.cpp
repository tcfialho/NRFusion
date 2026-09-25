#include "D3D10ExternalBridgeHarness.hpp"

namespace nrfusion::test {
namespace {

bool SoftwareAdapter(IDXGIAdapter1* adapter) noexcept {
    if (!adapter) return true;
    DXGI_ADAPTER_DESC1 desc{};
    return FAILED(adapter->GetDesc1(&desc)) ||
           (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

} // namespace

bool D3D10ExternalBridgeHarness::CreateDevices() {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return false;

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> candidate;
        const HRESULT enumerated =
            factory->EnumAdapters1(index, &candidate);
        if (enumerated == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(enumerated)) return false;
        if (SoftwareAdapter(candidate.Get())) continue;

        ComPtr<ID3D10Device1> candidate10;
        if (FAILED(D3D10CreateDevice1(
                candidate.Get(),
                D3D10_DRIVER_TYPE_HARDWARE,
                nullptr, 0,
                D3D10_FEATURE_LEVEL_10_0,
                D3D10_1_SDK_VERSION,
                &candidate10))) {
            continue;
        }

        D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        ComPtr<ID3D11Device> candidate11;
        ComPtr<ID3D11DeviceContext> candidateContext11;
        if (FAILED(D3D11CreateDevice(
                candidate.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr, 0,
                &level, 1,
                D3D11_SDK_VERSION,
                &candidate11, nullptr,
                &candidateContext11))) {
            continue;
        }

        ComPtr<ID3D12Device> candidate12;
        if (FAILED(D3D12CreateDevice(
                candidate.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&candidate12)))) {
            continue;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> candidateQueue;
        if (FAILED(candidate12->CreateCommandQueue(
                &queueDesc,
                IID_PPV_ARGS(&candidateQueue)))) {
            continue;
        }

        D3D11D3D12FenceBridge candidateBridge;
        if (!candidateBridge.BindAfterIdle(
                candidate11.Get(),
                candidateContext11.Get(),
                candidate12.Get(),
                candidateQueue.Get())) {
            continue;
        }

        adapter_ = candidate;
        device10_ = candidate10;
        device11_ = candidate11;
        context11_ = candidateContext11;
        device12_ = candidate12;
        queue12_ = candidateQueue;
        fenceBridge_ = std::move(candidateBridge);
        return true;
    }
    return false;
}

bool D3D10ExternalBridgeHarness::RouteContractMatches() const noexcept {
    D3D10BridgeRouteFacts facts{};
    facts.d3d10_1 = device10_ != nullptr;
    facts.sameAdapter =
        adapter_ && device11_ && device12_;
    facts.sourceTexture2D = true;
    facts.sourceFormat = ResourceFormat::Rgba16Float;
    facts.legacySharedSurface = true;
    facts.keyedMutex = true;
    facts.zeroTimeoutAcquire = true;
    facts.d3d11OpenLegacy = true;
    facts.d3d11NtSharedSurface = true;
    facts.d3d12OpenNtHandle = true;
    facts.gpuCopyInbound = true;
    facts.gpuCopyOutbound = true;
    facts.composeBackGpu = true;

    const auto route = QualifyD3D10BridgeRoute(facts);
    return route &&
           route.plan.inboundFullFrameCopies == 2 &&
           route.plan.outboundFullFrameCopies == 2 &&
           route.plan.usesLegacyDxgiHandle &&
           route.plan.usesNtHandleAfterD3D11 &&
           route.plan.usesZeroTimeoutKeyedMutex;
}

bool D3D10ExternalBridgeHarness::Open() {
    Close();
    return CreateDevices() && RouteContractMatches();
}

void D3D10ExternalBridgeHarness::Close() noexcept {
    fenceBridge_.ResetAfterIdle();
    queue12_.Reset();
    device12_.Reset();
    context11_.Reset();
    device11_.Reset();
    device10_.Reset();
    adapter_.Reset();
    carrierCopies_ = 0;
}

} // namespace nrfusion::test
