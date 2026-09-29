#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <vector>

namespace nrfusion {

class NgxGameProxyOverlay {
public:
    bool Draw(ID3D12GraphicsCommandList* commands,
              ID3D12Resource* output) noexcept;
    void Reset() noexcept;

private:
    struct OutputView {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    };

    bool EnsurePipeline(ID3D12Device* device) noexcept;
    ID3D12DescriptorHeap* ViewFor(
        ID3D12Device* device, ID3D12Resource* output) noexcept;

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    std::vector<OutputView> views_;
};

} // namespace nrfusion
