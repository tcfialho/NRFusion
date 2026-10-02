#include "nrfusion/OpenGlCarrierSync.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace nrfusion;

int main() {
    std::uint64_t next = 1;
    OpenGlCarrierSyncIdentity first{};
    assert(ReserveOpenGlCarrierSyncIdentity(
        11, 0, 3, next, first));
    assert(first.Valid(3));
    assert(first.workId == 11);
    assert(first.slotIndex == 0);
    assert(first.inputSignalValue == 1);
    assert(first.outputSignalValue == 2);
    assert(first.releaseSignalValue == 3);
    assert(next == 4);

    OpenGlCarrierSyncIdentity second{};
    assert(ReserveOpenGlCarrierSyncIdentity(
        12, 2, 3, next, second));
    assert(second.Valid(3));
    assert(second.inputSignalValue == 4);
    assert(second.outputSignalValue == 5);
    assert(second.releaseSignalValue == 6);
    assert(next == 7);

    OpenGlCarrierSyncIdentity invalid{};
    auto unchanged = next;
    assert(!ReserveOpenGlCarrierSyncIdentity(
        0, 0, 3, next, invalid));
    assert(next == unchanged);
    assert(!ReserveOpenGlCarrierSyncIdentity(
        13, 3, 3, next, invalid));

    next = std::numeric_limits<std::uint64_t>::max() - 2;
    OpenGlCarrierSyncIdentity terminal{};
    assert(ReserveOpenGlCarrierSyncIdentity(
        14, 1, 3, next, terminal));
    assert(terminal.Valid(3));
    assert(terminal.releaseSignalValue ==
           std::numeric_limits<std::uint64_t>::max());
    assert(next == 0);
    assert(!ReserveOpenGlCarrierSyncIdentity(
        15, 1, 3, next, invalid));
    return 0;
}
