#include "nrfusion/NrDeferredRetirementQueue.hpp"
#include "nrfusion/D3D12NrAllocationTracker.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <d3d12.h>
#else
struct ID3D12Fence {
    virtual unsigned long long GetCompletedValue() = 0;
    virtual unsigned long AddRef() = 0;
    virtual unsigned long Release() = 0;
    virtual ~ID3D12Fence() = default;
};
#endif

namespace {

std::size_t gAllocations = 0;

struct ReleaseLog {
    std::size_t features = 0;
    std::size_t resources = 0;
    void* last = nullptr;
};

void Release(void* context, nrfusion::NrRetiredObject retired) noexcept {
    auto& log = *static_cast<ReleaseLog*>(context);
    log.last = retired.object;
    if (retired.kind == nrfusion::NrRetiredObjectKind::Feature) ++log.features;
    else ++log.resources;
}

#if defined(_WIN32)
struct MockFence final : ID3D12Fence {
    ULONG refCount = 1;
    UINT64 completed = 0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) noexcept override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refCount; }
    ULONG STDMETHODCALLTYPE Release() noexcept override { return --refCount; }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) noexcept override { return E_NOTIMPL; }
    UINT64 STDMETHODCALLTYPE GetCompletedValue() noexcept override { return completed; }
    HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64, HANDLE) noexcept override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Signal(UINT64 val) noexcept override { completed = val; return S_OK; }
};
#else
struct MockFence final : ID3D12Fence {
    unsigned long refCount = 1;
    unsigned long long completed = 0;
    unsigned long long GetCompletedValue() override { return completed; }
    unsigned long AddRef() override { return ++refCount; }
    unsigned long Release() override { return --refCount; }
};
#endif

}

void* operator new(std::size_t size) {
    ++gAllocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

void* operator new[](std::size_t size) {
    ++gAllocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc();
}

void operator delete[](void* p) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    using namespace nrfusion;

    NrDeferredRetirementQueue queue;
    ReleaseLog log;

    void* invalidObject = reinterpret_cast<void*>(0x44);
    assert(!queue.Park(invalidObject, static_cast<NrRetiredObjectKind>(0xff)));
    assert(invalidObject == reinterpret_cast<void*>(0x44));
    assert(queue.Size() == 0);

    int feature = 1;
    void* featurePtr = &feature;
    assert(queue.Park(featurePtr, NrRetiredObjectKind::Feature));
    assert(featurePtr == nullptr);
    assert(queue.Size() == 1);

    for (std::uint32_t i = 1; i < NrDeferredRetirementQueue::kDefaultDelay; ++i) {
        queue.Tick(&log, Release);
        assert(log.features == 0);
        assert(queue.Size() == 1);
    }
    queue.Tick(&log, Release);
    assert(log.features == 1);
    assert(log.last == &feature);
    assert(queue.Size() == 0);

    int resource = 2;
    void* resourcePtr = &resource;
    assert(queue.Park(resourcePtr, NrRetiredObjectKind::Resource, 2, 4096, 65536));
    auto accounting = queue.ResourceAccounting();
    assert(accounting.resourceCount == 1);
    assert(accounting.logicalBytes == 4096);
    assert(accounting.physicalBytes == 65536);
    assert(accounting.logicalBytesExact);
    assert(accounting.physicalBytesExact);
    queue.Tick(&log, Release);
    assert(log.resources == 0);
    queue.Tick(&log, Release);
    assert(log.resources == 1);
    accounting = queue.ResourceAccounting();
    assert(accounting.resourceCount == 0);
    assert(accounting.logicalBytes == 0);
    assert(accounting.physicalBytes == 0);

    int held = 3;
    void* heldPtr = &held;
    assert(queue.Park(heldPtr, NrRetiredObjectKind::Feature, 1));
    queue.Tick(&log, nullptr);
    assert(queue.Size() == 1);
    queue.DrainAfterIdle(&log, Release);
    assert(log.features == 2);
    assert(queue.Size() == 0);

    void* nullPtr = nullptr;
    assert(!queue.Park(nullPtr, NrRetiredObjectKind::Feature));
    int invalidDelay = 4;
    void* invalidDelayPtr = &invalidDelay;
    assert(!queue.Park(invalidDelayPtr, NrRetiredObjectKind::Feature, 0));
    assert(invalidDelayPtr == &invalidDelay);

    int values[NrDeferredRetirementQueue::kCapacity + 1]{};
    void* pointers[NrDeferredRetirementQueue::kCapacity + 1]{};
    for (std::size_t i = 0; i < NrDeferredRetirementQueue::kCapacity + 1; ++i)
        pointers[i] = &values[i];

    for (std::size_t i = 0; i < NrDeferredRetirementQueue::kCapacity; ++i)
        assert(queue.Park(pointers[i], NrRetiredObjectKind::Feature, 1));
    assert(queue.Size() == NrDeferredRetirementQueue::kCapacity);
    assert(!queue.Park(pointers[NrDeferredRetirementQueue::kCapacity],
                       NrRetiredObjectKind::Feature, 1));
    assert(pointers[NrDeferredRetirementQueue::kCapacity] != nullptr);
    queue.Tick(&log, Release);
    assert(queue.Size() == 0);

    const std::size_t allocationsBefore = gAllocations;
    constexpr std::uint32_t kIterations = 100000;
    for (std::uint32_t i = 0; i < kIterations; ++i) {
        int value = 0;
        void* ptr = &value;
        assert(queue.Park(ptr, NrRetiredObjectKind::Resource, 1));
        queue.Tick(&log, Release);
    }
    assert(gAllocations == allocationsBefore);

    nrfusion::D3D12NrAllocationTracker::Instance().Clear();
    nrfusion::D3D12NrAllocationTracker::Instance().RecordAllocation(
        "TestOwner", "TestKind", DXGI_FORMAT_R16G16B16A16_FLOAT,
        1920, 1080, 1920 * 1080 * 8, 16777216, true, true);
    assert(nrfusion::D3D12NrAllocationTracker::Instance().TotalLogicalBytes() == 1920 * 1080 * 8);
    assert(nrfusion::D3D12NrAllocationTracker::Instance().TotalPhysicalBytes() == 16777216);
    assert(nrfusion::D3D12NrAllocationTracker::Instance().ActiveAllocationCount() == 1);
    nrfusion::D3D12NrAllocationTracker::Instance().RecordRetirement("TestOwner", "TestKind");
    assert(nrfusion::D3D12NrAllocationTracker::Instance().TotalLogicalBytes() == 0);
    assert(nrfusion::D3D12NrAllocationTracker::Instance().ActiveAllocationCount() == 0);

    // Fence-backed early retirement test:
    {
        MockFence fence;
        fence.completed = 10;
        int value = 42;
        void* ptr = &value;
        const std::size_t initialResources = log.resources;
        assert(queue.Park(ptr, NrRetiredObjectKind::Resource, 32, 1024, 4096, &fence, 10));
        assert(ptr == nullptr);
        assert(queue.Size() == 1);
        queue.Tick(&log, Release);
        // Completed fence causes immediate release on 1st tick!
        assert(queue.Size() == 0);
        assert(log.resources == initialResources + 1);
        assert(log.last == &value);
    }

    // Fence not completed yet, waits, then releases when signaled:
    {
        MockFence fence;
        fence.completed = 5;
        int value = 99;
        void* ptr = &value;
        const std::size_t initialResources = log.resources;
        assert(queue.Park(ptr, NrRetiredObjectKind::Resource, 32, 1024, 4096, &fence, 10));
        assert(queue.Size() == 1);
        queue.Tick(&log, Release);
        // Not completed (5 < 10), so not released yet:
        assert(queue.Size() == 1);
        assert(log.resources == initialResources);

        // Now fence completes:
        fence.completed = 10;
        queue.Tick(&log, Release);
        assert(queue.Size() == 0);
        assert(log.resources == initialResources + 1);
        assert(log.last == &value);
    }

    // Fence never completes: falls back to delay countdown:
    {
        MockFence fence;
        fence.completed = 0;
        int value = 123;
        void* ptr = &value;
        const std::size_t initialResources = log.resources;
        assert(queue.Park(ptr, NrRetiredObjectKind::Resource, 3, 1024, 4096, &fence, 999));
        assert(queue.Size() == 1);
        queue.Tick(&log, Release);
        assert(queue.Size() == 1); // 2 frames left
        queue.Tick(&log, Release);
        assert(queue.Size() == 1); // 1 frame left
        queue.Tick(&log, Release);
        assert(queue.Size() == 0); // Released by delay fallback!
        assert(log.resources == initialResources + 1);
    }

    // Zero allocations with fence under load:
    {
        MockFence fence;
        fence.completed = 1;
        const std::size_t allocsBefore = gAllocations;
        for (std::uint32_t i = 0; i < 10000; ++i) {
            int value = 0;
            void* ptr = &value;
            assert(queue.Park(ptr, NrRetiredObjectKind::Resource, 32, 0, 0, &fence, 1));
            queue.Tick(&log, Release);
        }
        assert(gAllocations == allocsBefore);
    }

    return 0;
}
