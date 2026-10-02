#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "NgxGameProxyOverlay.hpp"
#include "nrfusion/RuntimeOverlay.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace nrfusion {
namespace {

std::atomic<std::uint64_t> g_menuDraws{0};

const char* kMenuShader = R"(
RWTexture2D<float4> outputTexture : register(u0);
cbuffer MenuConstants : register(b0) {
    uint width;
    uint height;
    uint enabled;
    uint mode;
    uint targetFps;
    uint multiplier;
};

float3 Mix(float3 base, float3 overlay, float alpha) {
    return base * (1.0 - alpha) + overlay * alpha;
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint panelW = min(width, 540u);
    uint panelH = min(height, 390u);
    if (tid.x >= panelW || tid.y >= panelH) return;
    if (tid.x < 24 || tid.y < 24) return;

    uint2 p = tid.xy;
    float4 src = outputTexture[p];
    float3 colour = float3(0.035, 0.045, 0.065);
    float alpha = 0.82;
    bool edge = p.x < 29 || p.y < 29 ||
                p.x >= panelW - 5 || p.y >= panelH - 5;
    if (edge) {
        colour = float3(0.12, 0.42, 0.90);
        alpha = 0.96;
    }

    if (p.y >= 48 && p.y < 75 && p.x >= 48 && p.x < panelW - 28) {
        colour = float3(0.08, 0.20, 0.42);
        alpha = 0.92;
    }

    if (p.y >= 104 && p.y < 136 && p.x >= 48 && p.x < 238) {
        colour = enabled != 0
            ? float3(0.05, 0.55, 0.25)
            : float3(0.62, 0.12, 0.12);
        alpha = 0.94;
    }

    if (p.y >= 170 && p.y < 204 && p.x >= 48 && p.x < panelW - 28) {
        uint span = max(1u, panelW - 76);
        uint segment = min(3u, ((p.x - 48) * 4u) / span);
        colour = segment == mode
            ? float3(0.08, 0.46, 0.92)
            : float3(0.11, 0.13, 0.18);
        alpha = 0.92;
    }
    if (p.y >= 238 && p.y < 264 && p.x >= 48 && p.x < panelW - 28) {
        uint span = max(1u, panelW - 76);
        uint fill = min(span, (span * min(targetFps, 240u)) / 240u);
        colour = (p.x - 48) < fill
            ? float3(0.20, 0.66, 0.96)
            : float3(0.10, 0.12, 0.17);
        alpha = 0.92;
    }

    if (p.y >= 304 && p.y < 330 && p.x >= 48 && p.x < panelW - 28) {
        uint span = max(1u, panelW - 76);
        uint fill = min(span, (span * min(multiplier, 5u)) / 5u);
        colour = (p.x - 48) < fill
            ? float3(0.70, 0.34, 0.94)
            : float3(0.10, 0.12, 0.17);
        alpha = 0.92;
    }

    outputTexture[p] = float4(Mix(src.rgb, colour, alpha), src.a);
}
)";

bool SupportsTypedUav(ID3D12Device* device, DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    support.Format = format;
    return SUCCEEDED(device->CheckFeatureSupport(
               D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
           (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
}

} // namespace
bool NgxGameProxyOverlay::EnsurePipeline(ID3D12Device* device) noexcept {
    if (!device) return false;
    if (device_.Get() == device && root_ && pipeline_) return true;
    Reset();

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[0].DescriptorTable.NumDescriptorRanges = 1;
    parameters[0].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[1].Constants.ShaderRegister = 0;
    parameters[1].Constants.Num32BitValues = 6;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = parameters;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(
            &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
            &signature, &errors))) return false;
    if (FAILED(device->CreateRootSignature(
            0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&root_)))) return false;
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    errors.Reset();
    if (FAILED(D3DCompile(
            kMenuShader, std::strlen(kMenuShader), nullptr, nullptr, nullptr,
            "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0,
            &shader, &errors))) return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = root_.Get();
    pipelineDesc.CS = {
        shader->GetBufferPointer(), shader->GetBufferSize()};
    if (FAILED(device->CreateComputePipelineState(
            &pipelineDesc, IID_PPV_ARGS(&pipeline_)))) return false;

    device_ = device;
    return true;
}

ID3D12DescriptorHeap* NgxGameProxyOverlay::ViewFor(
    ID3D12Device* device, ID3D12Resource* output) noexcept {
    for (auto& view : views_)
        if (view.resource.Get() == output) return view.heap.Get();

    const auto desc = output->GetDesc();
    if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0 ||
        !SupportsTypedUav(device, desc.Format)) return nullptr;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 1;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    OutputView view{};
    view.resource = output;
    if (FAILED(device->CreateDescriptorHeap(
            &heapDesc, IID_PPV_ARGS(&view.heap)))) return nullptr;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = desc.Format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(
        output, nullptr, &uav,
        view.heap->GetCPUDescriptorHandleForHeapStart());
    views_.push_back(std::move(view));
    return views_.back().heap.Get();
}

bool NgxGameProxyOverlay::Draw(
    ID3D12GraphicsCommandList* commands, ID3D12Resource* output) noexcept {
    auto& overlay = RuntimeOverlay::Instance();
    if (!overlay.IsMenuOpen() || !commands || !output) return false;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(commands->GetDevice(IID_PPV_ARGS(&device))) ||
        !EnsurePipeline(device.Get())) return false;

    ID3D12DescriptorHeap* heap = ViewFor(device.Get(), output);
    if (!heap) return false;

    const auto desc = output->GetDesc();
    const auto draft = overlay.MenuDrawing().MainDraft();
    const auto status = overlay.MenuDrawing().StatusSnapshot();
    const std::uint32_t constants[6] = {
        static_cast<std::uint32_t>(desc.Width),
        desc.Height,
        draft.enabled ? 1u : 0u,
        static_cast<std::uint32_t>(draft.mode),
        static_cast<std::uint32_t>(std::max(0.0f, draft.targetFps)),
        status.effectiveMultiplier
    };
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = output;
    commands->ResourceBarrier(1, &barrier);

    commands->SetDescriptorHeaps(1, &heap);
    commands->SetComputeRootSignature(root_.Get());
    commands->SetPipelineState(pipeline_.Get());
    commands->SetComputeRootDescriptorTable(
        0, heap->GetGPUDescriptorHandleForHeapStart());
    commands->SetComputeRoot32BitConstants(1, 6, constants, 0);

    const UINT dispatchX =
        (std::min<UINT64>(desc.Width, 540) + 7) / 8;
    const UINT dispatchY =
        (std::min<UINT>(desc.Height, 390) + 7) / 8;
    commands->Dispatch(dispatchX, dispatchY, 1);
    commands->ResourceBarrier(1, &barrier);
    g_menuDraws.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void NgxGameProxyOverlay::Reset() noexcept {
    views_.clear();
    pipeline_.Reset();
    root_.Reset();
    device_.Reset();
}

extern "C" __declspec(dllexport) unsigned long long
NRFusion_MenuGpuDrawCount() noexcept {
    return g_menuDraws.load(std::memory_order_relaxed);
}

} // namespace nrfusion
