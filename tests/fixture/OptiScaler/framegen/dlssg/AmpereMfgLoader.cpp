#include <Config.h>
#include <misc/IdentifyGpu.h>
#include "AmpereMfgLoader.h"

std::string GenerateIniContent()
{
    auto* cfg = Config::Instance();
    int hwBilinear = cfg->FGDLSSGAmpereMfgHardwareBilinear.value_or_default() ? 1 : 0;
    return std::to_string(hwBilinear);
}

void TrySetup()
{
    auto* cfg = Config::Instance();
    if (!cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
        return;
}
