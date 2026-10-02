#include "D3D10ExternalBridgeHarness.hpp"

#include <windows.h>

#include <iostream>

using namespace nrfusion::test;

namespace {

constexpr int kSkip = 77;

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_D3D10_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

} // namespace

int main() {
    const bool required = HardwareRequired();
    D3D10ExternalBridgeHarness harness;
    if (!harness.Open())
        return required ? 1 : kSkip;

    if (!harness.RunReuseCycles(64)) {
        std::cerr << "d3d10 bridge: reuse cycles failed\n";
        return 2;
    }
    if (!harness.RunRecreationCycles(16)) {
        std::cerr << "d3d10 bridge: recreation cycles failed\n";
        return 3;
    }
    if (harness.CarrierCopyCount() != (64u + 16u) * 4u)
        return 4;

    harness.Close();
    return 0;
}
