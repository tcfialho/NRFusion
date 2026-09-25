#include "D3D9ExShareHarness.hpp"

#include <windows.h>

using namespace nrfusion::test;

namespace {

constexpr int kSkip = 77;

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_D3D9EX_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

} // namespace

int main() {
    const bool required = HardwareRequired();
    D3D9ExShareHarness harness;
    if (!harness.Open())
        return required ? 1 : kSkip;
    if (!harness.ProveSharedTexture(64, 64))
        return 2;
    if (!harness.ProveNonBlockingHandoff())
        return 3;
    if (!harness.ProveResetPersistence())
        return 4;
    if (!harness.ProveNonBlockingHandoff())
        return 5;
    harness.Close();
    return 0;
}
