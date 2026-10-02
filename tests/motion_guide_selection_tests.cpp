#include "nrfusion/MotionGuideSelection.hpp"

#include <cassert>

using namespace nrfusion;

int main() {
    MotionGuideAvailability guides{};
    assert(SelectMotionGuide(guides) == MotionSource::Zero);

    guides.shaderReliable = true;
    assert(SelectMotionGuide(guides) ==
           MotionSource::ShaderEstimated);

    guides.nvofAvailable = true;
    assert(SelectMotionGuide(guides) ==
           MotionSource::NvidiaOpticalFlow);

    guides.dlssContractReliable = true;
    assert(SelectMotionGuide(guides) ==
           MotionSource::DlssContract);

    guides.nativeReliable = true;
    assert(SelectMotionGuide(guides) ==
           MotionSource::Native);

    guides.cameraCut = true;
    assert(SelectMotionGuide(guides) == MotionSource::Zero);

    guides.cameraCut = false;
    guides.resetHistory = true;
    assert(SelectMotionGuide(guides) == MotionSource::Zero);
    return 0;
}
