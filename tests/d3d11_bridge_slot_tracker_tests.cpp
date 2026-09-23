#include "nrfusion/D3D11BridgeSlotTracker.hpp"

#include <cassert>

using namespace nrfusion;

int main() {
    D3D11BridgeSlotTracker tracker;
    assert(tracker.AllRetired(0));

    const auto first = tracker.Acquire(101, 0);
    const auto second = tracker.Acquire(102, 0);
    const auto third = tracker.Acquire(103, 0);
    assert(first && second && third);
    assert(!tracker.Acquire(104, 0));
    assert(!tracker.Acquire(101, 0));

    assert(tracker.MarkSubmitted(*first, 10));
    assert(tracker.MarkSubmitted(*second, 20));
    assert(tracker.MarkSubmitted(*third, 30));
    assert(!tracker.MarkSubmitted(*first, 11));
    assert(!tracker.Release(*second));
    assert(!tracker.AllRetired(29));

    assert(!tracker.Acquire(104, 9));
    const auto fourth = tracker.Acquire(104, 10);
    assert(fourth && fourth->slot == first->slot);
    assert(tracker.Find(104) == fourth->slot);
    assert(!tracker.Find(101));
    assert(tracker.MarkSubmitted(*fourth, 40));
    assert(!tracker.AllRetired(39));
    assert(tracker.AllRetired(40));

    const auto fifth = tracker.Acquire(105, 40);
    assert(fifth);
    assert(!tracker.AllRetired(100));
    assert(tracker.Release(*fifth));
    assert(!tracker.Find(105));
    assert(tracker.AllRetired(100));

    tracker.Reset();
    assert(tracker.AllRetired(0));
    assert(!tracker.Find(104));
    return 0;
}
