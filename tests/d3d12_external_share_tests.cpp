#include "D3D12ExternalShareHarness.hpp"

#include <windows.h>

#include <cassert>

using namespace nrfusion::test;

namespace {

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_D3D12_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

} // namespace

int main() {
    D3D12ExternalShareHarness harness;
    if (!harness.Open(HardwareRequired()))
        return HardwareRequired() ? 1 : 0;

    assert(harness.ColorHandle());
    assert(harness.OutputHandle());
    assert(harness.ProducerFenceHandle());
    assert(harness.ConsumerFenceHandle());
    assert(harness.AllocationSize() != 0);
    assert(harness.SignalProducer(1));

    harness.Close();
    return 0;
}
