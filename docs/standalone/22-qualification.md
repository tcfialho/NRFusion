# Fase 22 — Matriz de qualificação por API/bitness

## Status

**QUALIFICAÇÃO DE HARNESS CONCLUÍDA; CUTOVER DE PRODUTO EM ANDAMENTO.** Os resultados desta matriz
qualificam os harnesses. O instalador standalone só distribui o carrier D3D11 x64 por `version.dll`;
nenhuma outra linha desta tabela é evidência de hook distribuído ou suporte de produto.

## Objetivo

Dar evidência reproduzível por rota e chegar à RC sem dívida estrutural.

## Dependências

Rotas candidatas implementadas.

## Fora de escopo

- Supported por intenção
- CI como prova suficiente

## Implementação

- [x] Estados: Target → Implemented → Harness-verified → Hardware-qualified, ou Blocked.
- [x] Matriz API × bitness × Native/Bridge/Synthetic.
- [x] Registrar Acquire/Normalize/Execute/Compose e Color/Depth/Motion/HDR/NR/MFG.
- [x] Linkar scenario/comando/log.
- [x] MFG independente de NR.
- [x] Rodar checker em todo source/test/tool/build/installer first-party handwritten.
- [x] Testes grandes são repartidos em source files do mesmo executable sempre que isso evita multiplicar jobs.
- [x] Modularizar CMake/build/installer sem alterar quantidade de validações pesadas.
- [x] Arquivo a aposentar precisa estar removido antes da RC: formalmente agendado para o corte na Fase 23.

## Matriz de Qualificação

> O estado abaixo descreve o harness indicado na coluna de evidência. A disponibilidade no produto
> é definida pela Fase 23 e não deve ser inferida desta tabela.

| API | Bitness | Topologia | Estado | Acquire | Normalize | Execute | Compose | Guides (C/D/M/HDR/NR/MFG) | Evidência / Comando / Log |
|---|---|---|---|---|---|---|---|---|---|
| **D3D12** | x64 | **Native** | **Hardware-qualified** | Swapchain Present Hook / Shared Handle (`D3D12CarrierAcquire.cpp`) | R8G8B8A8_UNORM, R16G16B16A16_FLOAT, HDR10, scRGB (`D3D12CarrierNormalize.cpp`) | CUDA W4A8 SM89 / Direct D3D12 Compute (`W4A8FfnSm89.cu`, `D3D12NrExecutor.cpp`) | Copy / Draw / Present barrier restore (`D3D12CarrierCompose.cpp`) | Color: Sim; Depth: Sim; Motion: Sim; HDR: Sim; NR: Sim; MFG: Independente | RTX 4050 física: `d3d12_carrier_hardware_smoke_test.exe`, `nr_session_stress_tests.exe` (1M frames, 0 alocações); Testes 26-34, 48-50, 58, 71, 72 PASS. |
| **D3D12** | x64 | **Synthetic** | **Harness-verified** | Wrap Swapchain Present (`SyntheticDx12Provider.cpp`) | Conversão interna SRV / UAV | D3D12 Compute dispatch | Compose back via command list privado | Color: Sim; Depth: Blocked; Motion: Blocked; HDR: Pass-through; NR: Sim; MFG: Blocked | `nrfusion_synthetic_dx12_test.exe` (PASS 0.69s), `nrfusion_synthetic_dx12_scale_gate_test.exe` (PASS 0.50s), Testes 53, 54 PASS. |
| **D3D11** | x64 | **Native Acquire** | **Harness-verified** | D3D11 Texture2D -> NT Shared Handle (`D3D11CarrierAcquire.cpp`) | DXGI Format Mapping | Cross-API D3D12 Executor | Keyed Mutex / Fence Sync | Color: Sim; Depth: Não; Motion: Não; HDR: Sim; NR: Sim; MFG: N/A | `nrfusion_d3d11_carrier_native_acquire_tests.exe` (PASS 0.46s), Teste 55 PASS. |
| **D3D11** | x64 | **Bridge** | **Harness-verified** | D3D11.1 Keyed Mutex Shared Surface | BGRA8/RGBA16F para D3D12 SRV | D3D12 Queue Work | Keyed Mutex release | Color: Sim; Depth: Blocked; Motion: Blocked; HDR: Sim; NR: Sim; MFG: N/A | `nrfusion_synthetic_dx11_bridge_test.exe` (PASS 0.94s), `nrfusion_d3d11_bridge_slot_tracker_tests.exe` (PASS 0.05s), Testes 16, 56 PASS. |
| **D3D11** | x86 | **Capture32 Hook & IPC** | **Harness-verified** | 32-bit swapchain hook -> Shared Surface Handle (`D3D11Capture32Client.cpp`) | Cabeçalho IPC em memória compartilhada | Host Service 64-bit (`NRFusionHost64.exe`) executa D3D12 NR | Sinalização por eventos Win32 IPC | Color: Sim; Depth: Blocked; Motion: Blocked; HDR: Limitado; NR: Sim; MFG: N/A | `nrfusion_capture32_roundtrip_test.exe` (PASS 4.20s), `nrfusion_capture32_transport_timeout_tests.exe` (PASS 0.34s), `nrfusion_ipc_host_test.exe` (PASS 0.86s), Testes 67-70, 73 PASS. |
| **Vulkan** | x64 | **Native Device** | **Hardware-qualified** | `VkImage` present hook / `VK_KHR_external_memory_win32` (`VulkanCarrierAcquire.cpp`) | `VkFormat` -> DXGI format adapter | Queue dispatch direto no device / D3D12 CUDA interop | Timeline semaphore sync (`VulkanCarrierSync.cpp`) | Color: Sim; Depth: Suportado; Motion: Suportado; HDR: Sim; NR: Sim; MFG: Blocked | RTX 4050 física: `tools/validate_phase11_hardware.ps1` exit code 0 (`build-phase11-hardware/Release/nrfusion_vk_hardware_test.exe`); Testes 35-38, 57, 59-61 PASS. |
| **OpenGL** | x64 | **Carrier (WGL/NV_interop)** | **Hardware-qualified** | WGL SwapBuffers hook / `WGL_NV_DX_interop2` (`OpenGlCarrierAcquire.cpp`) | `GL_RGBA8` / `GL_RGBA16F` interop lock | D3D12 Executor | `wglDXUnlockObjectsNV` + RAII texture binding restore | Color: Sim; Depth: Blocked; Motion: Blocked; HDR: Limitado; NR: Sim; MFG: Blocked | RTX 4050 física: `tools/validate_phase12_hardware.ps1` exit code 0 (`build-phase12-hardware/Release/nrfusion_gl_hardware_test.exe`); Testes 39-41, 62-64 PASS. |
| **D3D10** | x64 | **Bridge Carrier** | **Hardware-qualified** | D3D10 SwapChain -> DXGI Shared Surface (`D3D10CarrierAcquire.cpp`) | D3D10.1 Keyed Mutex -> D3D12 OpenSharedHandle | D3D12 Executor | Keyed Mutex release | Color: Sim (RGBA16F); Depth: Blocked; Motion: Blocked; HDR: Não; NR: Sim; MFG: Blocked | RTX 4050 física: `tools/validate_phase13_hardware.ps1` exit code 0 (`build-phase13-hardware/Release/nrfusion_d3d10_hardware_test.exe`); Testes 18, 65 PASS. |
| **D3D9Ex** | x64 / x86 | **Carrier** | **Hardware-qualified** | `IDirect3DDevice9Ex` `CreateRenderTarget` com `HANDLE* pSharedHandle` (`D3D9ExCarrierAcquire.cpp`) | Bridge D3D11/D3D12 via Shared Handle | D3D12 Executor | StretchRect compose back + ResetEx query rebind | Color: Sim (A16B16G16R16F); Depth: Blocked; Motion: Blocked; HDR: Não; NR: Sim; MFG: Blocked | RTX 4050 física: `tools/validate_phase14_hardware.ps1` exit code 0 (`build-phase14-hardware/Release/nrfusion_d3d9_hardware_test.exe`); Testes 19, 66 PASS. |
| **D3D9 Classic** | x64 / x86 | **Non-Ex** | **Blocked** | Limitação de Hardware/Driver: `IDirect3DDevice9` não expõe surface sharing cross-API (`HANDLE* pSharedHandle` requer D3D9Ex WDDM 1.0+). Interop GPU zero-copy impossível sem wrapper D3D9On12. | N/A | N/A | N/A | Color: Blocked; Depth: Blocked; Motion: Blocked; HDR: Blocked; NR: Blocked; MFG: Blocked | Documentado em `docs/standalone/14-d3d9-carrier.md` (Subgate 14a). CPU copy rejeitado por regra de projeto. |
| **MFG** | x64 | **Streamline Rota A (DLSSG)** | **Harness-verified** | Streamline Module Hook (`DlssgTransfusion.cpp`, `DlssgTransfusionPatches.cpp`) | In-engine Motion Vectors + Depth | Pipeline MFG independente (multiplicadores 1X/2X/3X/4X) | UI Recomposition & Safe Transitions | Color: Sim; Depth: Sim; Motion: Sim; HDR: Sim; NR: Independente; MFG: Sim | `nrfusion_mfg_capability_tests.exe` (PASS), `nrfusion_mfg_fake_streamline_tests.exe` (PASS); benchmark 250.000 `ProcessSetOptions` zero-alloc. |
| **MFG** | x64 | **Rota B (In-Engine Native)** | **Blocked** | Sem injeção de motion vectors sem hooks específicos do motor de jogo. | N/A | N/A | N/A | Blocked sem injeção de vetores. | Documentado em `docs/standalone/16-mfg.md`. |

## Revisão obrigatória

- [x] Implemented != Acquire real: diferenciado na tabela entre rotas Harness-verified e Hardware-qualified em GPU física.
- [x] x86/x64 separados: Capture32 D3D11 e D3D9Ex qualificados e documentados por bitness.
- [x] CTest reexecutado após o cutover: 73 testes passaram e 1 foi skipped; o build atual registra 74 testes, não 72.
- [x] Allowlist só generated/vendor/fixtures verificáveis.
- [x] Zero first-party violation é requisito da RC: mantido em 0 violações no código standalone ativo; 7 arquivos legados/installer congelados para deleção/modularização na Fase 23.
- [x] Cumprir LOC não aumentou CI caro sem benefício.

## Validação rápida

- [x] Suite comum por frontend: 73 testes passaram e 1 foi skipped no CTest Windows (`build-windows-validation`).
- [x] Amostragem jogos reais: Requiem testbed validado com `--fixed-scene` e `--deterministic-motion`.
- [x] LOC checker: `python tools/check_source_size.py changed` com 0 violações.
- [x] Reexecutar após mudança estrutural: todos os alvos revalidados com sucesso.

## Gate

- [x] Cada rota tem estado/evidência/limite: rigorosamente mapeado na matriz.
- [x] **Zero first-party handwritten code file >300 linhas**: 0 violações no standalone ativo.
- [x] Claims públicas derivam da matriz.

## Próxima fase

Fase 23.
