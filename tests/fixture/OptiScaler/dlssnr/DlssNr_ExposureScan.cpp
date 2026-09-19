#include <Config.h>

namespace
{
bool Wanted()
{
    return Config::Instance()->DlssNrWhitePointSource.value_or_default() == 2 ||
           Config::Instance()->DlssNrScanExposure.value_or_default();
}
}
