# Fase 03 — Development Harness / MiniGame

## Status

**Concluída após segunda revisão adversarial.**
Evidência: [03-development-harness-evidence.md](03-development-harness-evidence.md).

## Objetivo

Transformar testes existentes no loop rápido principal sem criar novo game nem novo monólito de harness.

## Dependências

Fases 01–02.

## Fora de escopo

- Nova engine/harness paralelo
- Assets/física/câmera/UI
- Framework genérico de benchmark

## Implementação

- [x] Auditar `nrfusion_sim`, `nrfusion_harness_3d`, synthetic e IPC tests.
- [x] Dividir mecanicamente o atual `D3D12TestHarness.cpp` antes de expandi-lo.
- [x] Separar lifecycle/device, resources, pipeline/shader, scene, execution, provider, scenarios e metrics.
- [x] Evoluir o mesmo runner por CLI, sem criar outro D3D12 MiniGame.
- [x] Separar `correctness` de `benchmark`.
- [x] Manter waits/`Map`/console/filesystem fora do trecho medido do benchmark.
- [x] Adicionar steady/resize/reset/missing guides/provenance/on-off/failure.
- [x] Padronizar benchmark em p50/p95/p99 e counters com buffer pré-alocado.
- [x] Persistir baseline simples para relatório before/after automático.
- [x] Registrar legacy, `--scenario all` e `--benchmark` como CTests do mesmo executável.
- [x] Tornar a CLI fail-closed para opção inválida, valor ausente, zero e modos conflitantes.
- [x] Propagar host token, view e configuration generation no provider do harness.
- [x] Tornar reset um sinal one-shot real do provider.
- [x] Provar async inicial, fallback 61..90 e recuperação após frame 90.

## Revisão obrigatória

- [x] Harness chama policy/runtime existentes; não replica policy/executor.
- [x] Split preserva comportamento antes de adicionar features.
- [x] Métrica exclui setup/waits/`Map`/console/filesystem.
- [x] Nenhum CPU readback foi introduzido como atalho de produto.
- [x] Nenhum arquivo helpers genérico foi criado.
- [x] Regressão de split em `Halton()` encontrada pelo MSVC foi corrigida.
- [x] Closure flatten do patcher foi corrigido com `FrameContract.hpp`.
- [x] CTest legacy cobre a janela completa de fallback/recovery.
- [x] Cenário provenance valida a identidade adicional do FrameContract.

## Validação rápida

- [x] `nrfusion_sim` continua cobrindo loop CPU do controller.
- [x] Correctness possui lifecycle/config e resize real de recursos.
- [x] Benchmark mede Acquire + Validate + ResolveAuto.
- [x] Relatório before/after é automático quando existe baseline anterior.
- [x] Maior source novo/tocado na revisão = 243 linhas.
- [x] Portable Core final: PASS.
- [x] Windows integrated final: PASS.
- [x] CTest Windows: 18/18 PASS.
- [x] `nrfusion_harness_3d`: PASS com 120 frames.
- [x] `nrfusion_harness_3d_scenarios`: PASS.
- [x] `nrfusion_harness_3d_benchmark`: PASS.
- [x] CLI unknown/conflict/zero: fail-closed PASS.
- [x] Distribuição OptiScaler + NSIS: PASS.

## Gate

- [x] Loop implementar→executar→medir→revisar fechado em Windows.
- [x] Correctness/performance separados.
- [x] Harness existente ficou modular, não duplicado.
- [x] Nenhum arquivo da infraestrutura de harness tocada >300 linhas.
- [x] Segunda revisão adversarial sem blocker restante.

## Próxima fase

Fase 04 — somente após instrução explícita do usuário.
