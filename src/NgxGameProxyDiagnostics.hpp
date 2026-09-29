#pragma once

struct ID3D12GraphicsCommandList;

namespace nrfusion::ngxproxy {

void BeginDiagnosticPass(ID3D12GraphicsCommandList* commands) noexcept;
void EndDiagnosticPass(ID3D12GraphicsCommandList* commands, bool successful) noexcept;

} // namespace nrfusion::ngxproxy
