#pragma once
#include "nrfusion/NrDiagnosticsApi.hpp"

struct ID3D12GraphicsCommandList;

namespace nrfusion::ngxproxy {

void BeginDiagnosticPass(ID3D12GraphicsCommandList* commands) noexcept;
void EndDiagnosticPass(ID3D12GraphicsCommandList* commands, bool successful) noexcept;
RecordNrGpuStage DiagnosticGpuStageRecorder() noexcept;

} // namespace nrfusion::ngxproxy
