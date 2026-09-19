#include "nrfusion/FusedGroupedFfn.hpp"

// Builds without a CUDA compiler still see the interface, so callers never need to know
// whether the backend was compiled in: they ask, and get NoDevice.

namespace nrfusion {

bool FusedGroupedFfnAvailable() noexcept { return false; }

const char* Describe(GroupedFfnStatus status) noexcept {
    switch (status) {
    case GroupedFfnStatus::Ok: return "ok";
    case GroupedFfnStatus::NoDevice: return "build sem backend CUDA";
    case GroupedFfnStatus::UnsupportedShape: return "dimensoes nao sao multiplas do tile 16x16";
    case GroupedFfnStatus::OutOfSharedMemory: return "cadeia nao cabe em memoria compartilhada";
    case GroupedFfnStatus::LaunchFailed: return "lancamento falhou";
    }
    return "desconhecido";
}

GroupedFfnStatus LaunchFusedGroupedFfn(const GroupedFfnShape&, const GroupedFfnBuffers&, void*) {
    return GroupedFfnStatus::NoDevice;
}

} // namespace nrfusion
