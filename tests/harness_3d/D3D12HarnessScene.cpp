#include "D3D12TestHarness.hpp"

#include <directxmath.h>

#include <cstring>

namespace nrfusion::testing {

using namespace DirectX;

namespace {

struct SceneConstants {
    XMFLOAT4X4 currentWvp;
    XMFLOAT4X4 previousWvp;
    XMFLOAT4 jitterAndFlags;
};

} // namespace

void D3D12TestHarness::RenderScene(std::uint64_t frameIndex, float angleRad, Jitter jitter, bool cameraCut) {
    directAlloc_->Reset();
    directCmdList_->Reset(directAlloc_.Get(), pso_.Get());

    // Timestamp query: Start
    directCmdList_->EndQuery(timestampHeapDirect_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

    // Viewport & Scissor
    D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(config_.width), static_cast<float>(config_.height), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(config_.width), static_cast<LONG>(config_.height)};
    directCmdList_->RSSetViewports(1, &vp);
    directCmdList_->RSSetScissorRects(1, &scissor);

    // Render Targets
    const auto rtvHandleSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    rtvs[0] = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvs[1] = {rtvs[0].ptr + rtvHandleSize};
    rtvs[2] = {rtvs[1].ptr + rtvHandleSize};
    auto dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    const float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    const float clearMotion[2] = {0.0f, 0.0f};
    const float clearReactive[1] = {0.0f};
    directCmdList_->ClearRenderTargetView(rtvs[0], clearColor, 0, nullptr);
    directCmdList_->ClearRenderTargetView(rtvs[1], clearMotion, 0, nullptr);
    directCmdList_->ClearRenderTargetView(rtvs[2], clearReactive, 0, nullptr);
    directCmdList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    directCmdList_->OMSetRenderTargets(3, rtvs, FALSE, &dsv);
    directCmdList_->SetGraphicsRootSignature(rootSig_.Get());

    // Matrices
    XMMATRIX world = XMMatrixRotationRollPitchYaw(angleRad * 0.7f, angleRad, 0.0f);
    XMVECTOR eye = XMVectorSet(0.0f, 1.5f, -3.5f, 0.0f);
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = static_cast<float>(config_.width) / static_cast<float>(config_.height);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 100.0f);

    XMMATRIX wvp = XMMatrixMultiply(world, XMMatrixMultiply(view, proj));
    SceneConstants constants;
    XMStoreFloat4x4(&constants.currentWvp, XMMatrixTranspose(wvp));

    if (!hasPrevFrame_ || cameraCut) {
        constants.previousWvp = constants.currentWvp;
    } else {
        memcpy(&constants.previousWvp, prevViewProj_, sizeof(prevViewProj_));
    }
    memcpy(prevViewProj_, &constants.currentWvp, sizeof(prevViewProj_));
    hasPrevFrame_ = true;

    constants.jitterAndFlags = XMFLOAT4(jitter.x, jitter.y, cameraCut ? 1.0f : 0.0f, 0.0f);
    directCmdList_->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstants) / 4, &constants, 0);

    directCmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    directCmdList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    directCmdList_->DrawInstanced(36, 1, 0, 0);

    // Timestamp query: End
    directCmdList_->EndQuery(timestampHeapDirect_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    directCmdList_->ResolveQueryData(timestampHeapDirect_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                     timestampReadbackDirect_.Get(), 0);

    directCmdList_->Close();
    ID3D12CommandList* lists[] = {directCmdList_.Get()};
    directQueue_->ExecuteCommandLists(1, lists);
}


} // namespace nrfusion::testing
