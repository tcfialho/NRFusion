#include "FakeTimingSource.hpp"

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

std::size_t gAllocations = 0;

nrfusion::WorkTicket Ticket(std::uint64_t id) {
    return {id, 3, 100 + id, id, 9, 1.0f, 8};
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
    using nrfusion::test::FakeTimingSource;

    FakeTimingSource source(2);
    const WorkTicket a = Ticket(1);
    const WorkTicket b = Ticket(2);
    const WorkTicket c = Ticket(3);

    assert(source.Push(a, 1.25, 10));
    assert(source.Push(b, 2.50, 20));
    assert(!source.Push(c, 3.75, 30));
    assert(!source.TryRetire(9));

    const auto retiredA = source.TryRetire(10);
    assert(retiredA && retiredA->mapsWork);
    assert(retiredA->ticket == a);
    assert(retiredA->gpuMs == 1.25);

    assert(source.Push(c, 3.75, 30));
    assert(!source.Push(Ticket(4), 4.0, 30));
    assert(!source.TryRetire(19));

    const auto retiredB = source.TryRetire(20);
    assert(retiredB && retiredB->ticket == b);
    const auto retiredC = source.TryRetire(30);
    assert(retiredC && retiredC->ticket == c);
    assert(source.Size() == 0);

    assert(source.PushInvalid(40));
    const auto invalid = source.TryRetire(40);
    assert(invalid && !invalid->mapsWork);
    assert(invalid->ticket.id == 0);

    source.ResetAfterIdle();
    assert(source.Size() == 0);
    assert(source.Push(a, 1.0, 1));

    source.ResetAfterIdle();
    const std::size_t allocationsBefore = gAllocations;
    for (std::uint64_t i = 1; i <= 100000; ++i) {
        WorkTicket ticket = Ticket(i);
        assert(source.Push(ticket, 2.0, i));
        const auto sample = source.TryRetire(i);
        assert(sample && sample->ticket == ticket);
    }
    assert(gAllocations == allocationsBefore);
    return 0;
}
