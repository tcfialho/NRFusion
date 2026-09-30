#pragma once
#include <cassert>
#include <d3d12.h>
#include <wrl/client.h>

namespace nrfusion::testing {
class CodecApiCounters {
public:
    unsigned maps = 0;
    unsigned unmaps = 0;
    unsigned cbvs = 0;
    unsigned failAtMap = 0;

    explicit CodecApiCounters(ID3D12Device* device) {
        assert(active_ == nullptr);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 256;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        assert(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload_))));
        auto** resourceMethods = *reinterpret_cast<void***>(upload_.Get());
        auto** deviceMethods = *reinterpret_cast<void***>(device);
        mapSlot_ = &resourceMethods[8];
        unmapSlot_ = &resourceMethods[9];
        cbvSlot_ = &deviceMethods[17];
        originalMap_ = reinterpret_cast<MapFunction>(*mapSlot_);
        originalUnmap_ = reinterpret_cast<UnmapFunction>(*unmapSlot_);
        originalCbv_ = reinterpret_cast<CbvFunction>(*cbvSlot_);
        active_ = this;
        Replace(mapSlot_, reinterpret_cast<void*>(&Map));
        Replace(unmapSlot_, reinterpret_cast<void*>(&Unmap));
        Replace(cbvSlot_, reinterpret_cast<void*>(&CreateCbv));
    }

    ~CodecApiCounters() {
        Replace(mapSlot_, reinterpret_cast<void*>(originalMap_));
        Replace(unmapSlot_, reinterpret_cast<void*>(originalUnmap_));
        Replace(cbvSlot_, reinterpret_cast<void*>(originalCbv_));
        active_ = nullptr;
    }
    CodecApiCounters(const CodecApiCounters&) = delete;
    CodecApiCounters& operator=(const CodecApiCounters&) = delete;

private:
    using MapFunction = HRESULT(STDMETHODCALLTYPE*)(ID3D12Resource*, UINT, const D3D12_RANGE*, void**);
    using UnmapFunction = void(STDMETHODCALLTYPE*)(ID3D12Resource*, UINT, const D3D12_RANGE*);
    using CbvFunction = void(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
    static inline CodecApiCounters* active_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_;
    void** mapSlot_ = nullptr;
    void** unmapSlot_ = nullptr;
    void** cbvSlot_ = nullptr;
    MapFunction originalMap_ = nullptr;
    UnmapFunction originalUnmap_ = nullptr;
    CbvFunction originalCbv_ = nullptr;

    static void Replace(void** slot, void* function) {
        DWORD protection = 0;
        assert(VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection));
        InterlockedExchangePointer(slot, function);
        DWORD ignored = 0;
        assert(VirtualProtect(slot, sizeof(void*), protection, &ignored));
    }
    static HRESULT STDMETHODCALLTYPE Map(ID3D12Resource* resource, UINT subresource,
        const D3D12_RANGE* range, void** mapped) {
        ++active_->maps;
        if (active_->failAtMap && active_->maps == active_->failAtMap) {
            if (mapped) *mapped = nullptr;
            return E_FAIL;
        }
        return active_->originalMap_(resource, subresource, range, mapped);
    }
    static void STDMETHODCALLTYPE Unmap(ID3D12Resource* resource, UINT subresource, const D3D12_RANGE* range) {
        ++active_->unmaps;
        active_->originalUnmap_(resource, subresource, range);
    }
    static void STDMETHODCALLTYPE CreateCbv(ID3D12Device* device, const D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,
        D3D12_CPU_DESCRIPTOR_HANDLE handle) {
        ++active_->cbvs;
        active_->originalCbv_(device, desc, handle);
    }
};
}
