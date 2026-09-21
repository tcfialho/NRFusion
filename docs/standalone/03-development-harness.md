# Fase 03 — Development Harness / MiniGame

## Status

**Implementação estrutural concluída; execução Windows pendente.**
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
- [x] Adicionar só cenários faltantes: steady/resize/reset/missing guides/provenance/on-off/failure.
- [x] Padronizar benchmark em p50/p95/p99 e counters com buffer pré-alocado.
- [x] Persistir baseline simples para relatório before/after automático.
- [x] Manter instrumentação local mínima.
- [x] Não tocar nos testes legados >300 que não precisavam mudar.

## Revisão obrigatória

- [x] Harness chama policy/runtime existentes; não replica policy/executor.
- [x] Split preserva comportamento antes de adicionar features.
- [x] Métrica exclui setup/waits/`Map`/console/filesystem.
- [x] Nenhum CPU readback foi introduzido como atalho de produto.
- [x] Nenhum arquivo helpers genérico foi criado.

## Validação rápida

- [x] `nrfusion_sim` continua cobrindo loop CPU do controller.
- [x] Correctness possui lifecycle/config e resize real de recursos.
- [x] Benchmark declara Acquire + Validate + ResolveAuto como trecho medido.
- [x] Relatório before/after é automático quando existe baseline anterior.
- [x] Auditoria equivalente ao checker: todo source novo/tocado <=300.
- [ ] Compilar e executar o harness Windows/D3D12.

## Gate

- [ ] Loop implementar→executar→medir→revisar fechado em Windows.
- [x] Correctness/performance separados.
- [x] Harness existente ficou modular, não duplicado.
- [x] Nenhum arquivo da infraestrutura de harness tocada >300 linhas.

## Próxima ação

Executar uma validação Windows estabilizada do lote, sem iniciar a Fase 04.
