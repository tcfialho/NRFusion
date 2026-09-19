#include "pch.h"
#include "FrameLimit.h"
#include "Config.h"
void FrameLimit::sleep(bool fgActive)
{
    if (auto fpsCap = Config::Instance()->FramerateLimit.value_or_default(); fpsCap != 0.0f)
    {
        uint64_t min_interval_us = std::clamp((uint64_t) (1'000'000 / fpsCap), 0ULL, 100'000'000ULL);

        if (fgActive)
            min_interval_us *= 2;
        use(min_interval_us);
    }
}
