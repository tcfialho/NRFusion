#pragma once
#include "nrfusion/StreamlineDlssgHook.hpp"

namespace nrfusion::streamline {
void InterceptDlssgFunction(const char* name, void** function) noexcept;
void RequestOptionsUpdate() noexcept;
StreamlineMfgStatus ReadMfgStatus() noexcept;
} // namespace nrfusion::streamline
