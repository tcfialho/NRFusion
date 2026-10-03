#pragma once
#include "nrfusion/StreamlineDlssgHook.hpp"

namespace nrfusion::streamline {
void SetFeatureDispatcher(void* dispatcher) noexcept;
void InterceptDlssgFunction(const char* name, void** function) noexcept;
void InterceptReflexFunction(const char* name, void** function) noexcept;
void InterceptPclFunction(const char* name, void** function) noexcept;
void RequestOptionsUpdate() noexcept;
StreamlineMfgStatus ReadMfgStatus() noexcept;
} // namespace nrfusion::streamline
