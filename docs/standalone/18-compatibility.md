# Fase 18 — State restoration e compatibilidade

## Objetivo

Isolar compatibility exceptions em módulos pequenos por causa/estado, sem framework monolítico.

## Dependências

Carriers principais.

## Fora de escopo

- Copiar hooks genéricos do OptiScaler.
- Tracking global default.
- Criar profile por jogo para mascarar leak geral.

## Auditoria

- O caminho DLSS NR em D3D12 pode substituir bindings do command list.
- `tools/requiem_game/main.cpp` já restaura manualmente PSO, graphics root signature,
  descriptor heaps, root table/constants, OM/raster/IA depois da avaliação.
- `D3D12NrExecutor` restaura resource states, mas não consegue consultar bindings anteriores:
  D3D12 não oferece getters para root signatures, PSO ou root arguments.
- `D3D12CarrierExecutor` recebe command list emprestado e utiliza estado fornecido pelo
  caller quando esse caller pretende continuar gravando na mesma lista.
- O synthetic D3D12 do patcher usa `SyntheticCommandContext::list`, command list privado;
  o clobber interno do provider não é leak ativo do command list do jogo nesse caminho.
- `CompatibilityDatabase.cpp` (350 linhas) e `ProfileStore.cpp` (666 linhas) ficaram
  intocados e congelados para eliminação no cutover. Nenhuma exception por jogo foi adicionada.
- OpenGL standalone preserva e restaura o binding anterior via RAII (`GL_TEXTURE_BINDING_2D`).
- D3D11 x64 standalone usa apenas CopyResource/fences nos owners auditados; não grava bindings.
- D3D10/D3D9 owners auditados são route/acquire/sync; não gravam pipeline bindings.
- Vulkan usa estado explícito de command buffer e não precisa de tracker implícito equivalente.

## Implementação

- [x] Identificar states necessários para o boundary D3D12 auditado.
- [x] Separar state restore de profile matching/database.
- [x] Restore opt-in de descriptor heaps, graphics/compute root signature e PSO.
- [x] Root params/estado adicional usam callback caller-owned, sem alocação.
- [x] Nenhum map/mutex global no fast path.
- [x] Default `commandStateRestore == nullptr` não chama helper nem API D3D12.
- [x] Restore roda depois de `ExecuteFrame()`, inclusive quando o executor retorna falha.
- [x] Config de restore inválida falha antes de emitir restore parcial.
- [x] OpenGL preserva `GL_TEXTURE_BINDING_2D` em criação/recriação e early-return.
- [x] Ligar restore no boundary do `D3D12CarrierExecutor`: validado em `nrfusion_d3d12_command_state_restore_tests`.
- [x] Restore do Requiem testbed isolado em módulo <= 300 linhas.
- [x] State preservation do hook D3D11 x86 desacoplado via IPC em processo separado (`NRFusionHost64.exe`).

## Split estrutural

- `src/SyntheticDx12Provider.cpp` legado permanece congelado para cutover da Fase 23.
- O standalone compila:
  - `SyntheticDx12ProviderLifecycle.cpp`
  - `SyntheticDx12ProviderSubmit.cpp`
  - `SyntheticDx12ProviderResidual.cpp`
- Todos os novos owners ficam <=300 linhas.

## Revisão obrigatória

- [x] Nenhum profile mascara o leak geral.
- [x] Restore é chamado após sucesso/falha do executor quando habilitado.
- [x] Exceção não impõe chamadas D3D12 no default.
- [x] Split por estado/ownership, não por game arbitrariamente.
- [x] Integração do caller validada via contratos e harness state restore.

## Validação rápida

- [x] Harness WARP clobbera heap/root signature/PSO/root table e prova restore por dispatch.
- [x] Restore inválido é rejeitado fail-closed.
- [x] Benchmark default vs compatibility existe no harness.
- [x] Harness OpenGL sem hardware prova restore normal e em early-return.
- [x] LOC checker: 0 violações no código ativo.
- [x] Harness integrado testado via `nrfusion_d3d12_command_state_restore_tests`.

## Evidência

- `07dfe42`: split standalone do SyntheticDx12Provider; 3/3 CI PASS.
- `5d2ebda`: restore caller-owned no boundary do D3D12 carrier; 3/3 CI PASS.
- `1ab383b`: benchmark default vs compatibility; 3/3 CI PASS.
- `fb8e338`: fast path nulo sem chamada ao helper.
- `f0ea299`: preservação do texture binding OpenGL.
- `nrfusion_d3d12_command_state_restore_tests`: PASS (0.08 sec).
- `nrfusion_opengl_state_restore_tests`: PASS (0.02 sec).

## Gate

- [x] Default não executa restore nem API D3D12 adicional.
- [x] Módulo novo de restore respeita <=300 linhas.
- [x] CompatibilityDatabase/ProfileStore oversized não foram tocados (congelados para descarte na Fase 23).
- [x] Caller do carrier qualificado com state restoration comprovado.

**Primitivas e harnesses da Fase 18 concluídos; o caller distribuído D3D12 permanece pendente.**
O cutover atual distribui somente o caller D3D11→Host64 da Fase 23.

## Próxima fase

Fase 19.
