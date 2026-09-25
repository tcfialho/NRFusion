#include "OpenGlExternalInteropHarness.hpp"

#include <windows.h>

using namespace nrfusion::test;

namespace {

constexpr int kSkip = 77;

bool HardwareRequired() {
    char value[2]{};
    return GetEnvironmentVariableA(
        "NRFUSION_TEST_OPENGL_HARDWARE",
        value, static_cast<DWORD>(sizeof(value))) != 0;
}

} // namespace

int main() {
    const bool required = HardwareRequired();
    OpenGlExternalInteropHarness harness;
    if (!harness.Open())
        return required ? 1 : kSkip;

    if (!harness.RunRecreationCycles(32) ||
        !harness.RunReuseCycles(128)) {
        harness.Close();
        return 2;
    }
    harness.Close();

    if (!harness.Open())
        return required ? 3 : kSkip;
    const bool recreated =
        harness.RunRecreationCycles(2);
    harness.Close();
    return recreated ? 0 : 4;
}
