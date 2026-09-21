#include "nrfusion/NrSubmissionGate.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using nrfusion::NrSubmissionGate;

    NrSubmissionGate gate;
    assert(!gate.Pending());
    assert(gate.CreateEpoch() == 0);
    assert(gate.ReadyFor(0));
    assert(gate.ReadyFor(1));

    gate.MarkCreated(10);
    assert(gate.Pending());
    assert(gate.CreateEpoch() == 10);
    assert(!gate.ReadyFor(10));
    assert(!gate.ReadyFor(9));
    assert(gate.Pending());

    assert(gate.ReadyFor(11));
    assert(!gate.Pending());
    assert(gate.ReadyFor(11));
    assert(gate.ReadyFor(10));

    gate.MarkCreated(20);
    assert(!gate.ReadyFor(20));
    gate.MarkCreated(25);
    assert(gate.Pending());
    assert(gate.CreateEpoch() == 25);
    assert(!gate.ReadyFor(21));
    assert(!gate.ReadyFor(25));
    assert(gate.ReadyFor(26));

    gate.MarkCreated(UINT64_MAX);
    assert(gate.Pending());
    assert(!gate.ReadyFor(UINT64_MAX));
    assert(!gate.ReadyFor(0));

    gate.Reset();
    assert(!gate.Pending());
    assert(gate.CreateEpoch() == 0);
    assert(gate.ReadyFor(0));

    constexpr std::uint64_t kIterations = 1000000;
    for (std::uint64_t i = 1; i <= kIterations; ++i) {
        gate.MarkCreated(i);
        assert(!gate.ReadyFor(i));
        assert(gate.ReadyFor(i + 1));
    }

    return 0;
}
