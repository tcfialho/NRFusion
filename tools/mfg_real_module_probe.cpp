#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "nrfusion/DlssgTransfusion.hpp"

#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"usage: nrfusion_mfg_real_module_probe <nvngx_dlssg.dll>\n";
        return 2;
    }

    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) {
        std::wcerr << L"LoadLibrary failed: " << GetLastError() << L"\n";
        return 3;
    }

    auto& transfusion = nrfusion::DlssgTransfusion::Instance();
    transfusion.TryApply(module);
    const auto status = transfusion.Status();

    std::cout
        << "module=" << status.moduleFound
        << " gates=" << status.archGatesPatched
        << " advertise=" << status.advertiseGatePatched
        << " validate=" << status.validateGatePatched
        << " kernels=" << status.blackwellKernelsRewritten
        << " kept_stock=" << status.blackwellKernelsKeptStock
        << " selector=" << static_cast<uint32_t>(status.kernelSelector)
        << " unlocked_max=" << transfusion.UnlockedMax()
        << " blackwell=" << status.blackwellTransfusionActive
        << " uir=" << status.uirPatched
        << " failure=" << status.failureReason
        << "\n";

    const bool ok =
        status.moduleFound &&
        status.archGatesPatched &&
        status.advertiseGatePatched &&
        status.validateGatePatched &&
        (status.kernelSelector == nrfusion::MfgKernelSelector::StockOnly ||
         (status.blackwellTransfusionActive && status.blackwellKernelsRewritten > 0)) &&
        transfusion.UnlockedMax() >= 3;
    FreeLibrary(module);
    return ok ? 0 : 1;
}
