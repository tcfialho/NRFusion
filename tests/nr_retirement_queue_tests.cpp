#include "nrfusion/NrDeferredRetirementQueue.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

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
    assert(queue.Park(resourcePtr, NrRetiredObjectKind::Resource, 2));
    queue.Tick(&log, Release);
    assert(log.resources == 0);
    queue.Tick(&log, Release);
    assert(log.resources == 1);

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

    return 0;
}
