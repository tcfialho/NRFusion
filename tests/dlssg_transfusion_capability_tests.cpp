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

    transfusion.SetControlMode(nrfusion::MfgControlMode::FollowGame);
    std::uint32_t mode = 0;
    std::uint32_t frames = 1;
    transfusion.ProcessSetOptions(mode, frames);
    assert(frames == 1);

    for (int i = 0; i < 7; ++i)
    {
        frames = 3;
        transfusion.ProcessSetOptions(mode, frames);
        assert(frames == 1);
    }

    frames = 3;
    transfusion.ProcessSetOptions(mode, frames);
    assert(frames == 3);

    const auto transitioned = transfusion.Status();
    assert(transitioned.requestedByGame == 3);
    assert(transitioned.effectiveMultiplier == 4);

    return 0;
}
