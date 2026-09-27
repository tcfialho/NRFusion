#include "nrfusion/HostServer64.hpp"
#include "D3D12TestDevice.hpp"

#include <cassert>
#include <cstddef>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace nrfusion {

struct HostServer64StopTestAccess {
    static bool Prepare(HostServer64& host, ID3D12Device* device) {
        host.d3d12Device_ = device;

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(
                &queueDesc, IID_PPV_ARGS(&host.d3d12Queue_))) ||
            FAILED(device->CreateFence(
                0, D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&host.d3d12Fence_))) ||
            FAILED(device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&host.d3d12Alloc_)))) {
            return false;
        }

        if (FAILED(device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                host.d3d12Alloc_.Get(), nullptr,
                IID_PPV_ARGS(&host.d3d12CmdList_))) ||
            FAILED(host.d3d12CmdList_->Close())) {
            return false;
        }

        host.syntheticProvider_ = std::make_unique<SyntheticDx12Provider>();
        ProviderContext context{};
        context.api = GraphicsApi::D3D12;
        context.device = device;
        context.commandQueue = host.d3d12Queue_.Get();
        if (!host.syntheticProvider_->Initialize(context)) return false;
        host.dlssNr_ = std::make_unique<HostDlssNr>();
        return host.EnsureZeroGuides(64, 64);
    }

    static std::size_t OwnerCount(const HostServer64& host) {
        return std::size_t(host.d3d12Device_ != nullptr) +
            std::size_t(host.d3d12Queue_ != nullptr) +
            std::size_t(host.d3d12Alloc_ != nullptr) +
            std::size_t(host.d3d12CmdList_ != nullptr) +
            std::size_t(host.d3d12Fence_ != nullptr) +
            std::size_t(host.zeroGuideUpload_ != nullptr) +
            std::size_t(host.lowGuideDepth_ != nullptr) +
            std::size_t(host.lowGuideMotion_ != nullptr) +
            std::size_t(host.guideAlloc_ != nullptr) +
            std::size_t(host.guideCmdList_ != nullptr) +
            std::size_t(host.guideFence_ != nullptr) +
            std::size_t(host.syntheticProvider_ != nullptr) +
            std::size_t(host.dlssNr_ != nullptr);
    }
};

} // namespace nrfusion

int main() {
    using namespace nrfusion;

    ComPtr<ID3D12Device> device = testing::CreateD3D12TestDevice();
    assert(device);

    HostServer64 host;
    assert(HostServer64StopTestAccess::Prepare(host, device.Get()));
    assert(HostServer64StopTestAccess::OwnerCount(host) >= 10);

    host.Stop();
    assert(!host.IsRunning());
    assert(HostServer64StopTestAccess::OwnerCount(host) == 0);

    host.Stop();
    assert(HostServer64StopTestAccess::OwnerCount(host) == 0);
    return 0;
}
