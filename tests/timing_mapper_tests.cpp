#include "nrfusion/TimingWorkMapper.hpp"

#include <cassert>

using namespace nrfusion;

namespace {
WorkTicket Ticket(std::uint64_t id) {
    return WorkTicket{id, 1, id, 0, 1, 1.0f, 0};
}
}

int main() {
    TimingWorkMapper mapper(3);
    const auto a = Ticket(1);
    const auto b = Ticket(2);
    const auto c = Ticket(3);
    const auto d = Ticket(4);
    const auto e = Ticket(5);
    const auto f = Ticket(6);

    assert(mapper.Capacity() == 3);
    assert(!mapper.Push(a));
    assert(!mapper.PushInvalid());
    assert(!mapper.Push(b));
    assert(mapper.Size() == 3);

    const auto dropped = mapper.Push(c);
    assert(dropped && dropped->id == a.id);
    assert(mapper.Size() == 3);

    const auto invalid = mapper.Pop();
    assert(invalid && !invalid->mapsWork);
    const auto second = mapper.Pop();
    assert(second && second->mapsWork && second->ticket.id == b.id);

    assert(!mapper.Push(d));
    assert(!mapper.Push(e));
    assert(mapper.Size() == 3);

    const auto wrappedDrop = mapper.Push(f);
    assert(wrappedDrop && wrappedDrop->id == c.id);

    const auto first = mapper.Pop();
    const auto next = mapper.Pop();
    const auto last = mapper.Pop();
    assert(first && first->ticket.id == d.id);
    assert(next && next->ticket.id == e.id);
    assert(last && last->ticket.id == f.id);
    assert(!mapper.Pop());

    mapper.Push(a);
    mapper.Reset();
    assert(mapper.Size() == 0);
    assert(!mapper.Pop());

    return 0;
}
