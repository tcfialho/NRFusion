#include "TestCoordinator.hpp"
#include "nrfusion/Presets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace nrfusion {

#include "TestCoordinatorCore.inc"
#include "TestCoordinatorWork.inc"
#include "TestCoordinatorTiming.inc"
#include "TestCoordinatorAsync.inc"

} // namespace nrfusion
