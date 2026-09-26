#include "nrfusion/DlssgTransfusion.hpp"

#include <cassert>
#include <cstdint>

int main()
{
    auto& transfusion = nrfusion::DlssgTransfusion::Instance();

    const auto status = transfusion.Status();
    assert(!status.moduleFound);
    assert(!status.advertiseGatePatched);
    assert(!status.validateGatePatched);
    assert(status.blackwellKernelsRewritten == 0);
    assert(transfusion.UnlockedMax() == 0);

    std::uint32_t stockMax = 3;
    transfusion.ProcessGetState(stockMax);
    assert(stockMax == 3);

    std::uint32_t unavailableMax = 0;
    transfusion.ProcessGetState(unavailableMax);
    assert(unavailableMax == 0);

    return 0;
}
