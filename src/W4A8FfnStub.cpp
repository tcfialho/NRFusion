#include "nrfusion/W4A8Ffn.hpp"

namespace nrfusion {

bool AdaW4A8Available() noexcept {
    return false;
}

const char* Describe(AdaW4A8Status status) noexcept {
    switch (status) {
    case AdaW4A8Status::Ok: return "ok";
    case AdaW4A8Status::NoDevice: return "build sem backend CUDA";
    case AdaW4A8Status::UnsupportedArchitecture: return "dispositivo nao e SM89 (Ada)";
    case AdaW4A8Status::InvalidParams: return "parametros de lancamento invalidos";
    case AdaW4A8Status::LaunchFailed: return "falha no lancamento do kernel";
    }
    return "desconhecido";
}

AdaW4A8Status LaunchAdaW4A8Ffn(const AdaW4A8Params&) {
    return AdaW4A8Status::NoDevice;
}

AdaBenchmarkResult BenchmarkAdaW4A8Ffn(const AdaW4A8Params&, float) {
    return AdaBenchmarkResult{};
}

} // namespace nrfusion
