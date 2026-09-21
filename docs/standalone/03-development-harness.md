# Fase 03 — Development Harness / MiniGame

## Objetivo

Transformar testes existentes no loop rápido principal sem criar novo game nem novo monólito de harness.

## Dependências

Fases 01–02.

## Fora de escopo

- Nova engine/harness paralelo
- Assets/física/câmera/UI
- Framework genérico de benchmark

## Implementação

- [ ] Auditar `nrfusion_sim`, `nrfusion_harness_3d`, synthetic e IPC tests.
- [ ] Dividir mecanicamente o atual `D3D12TestHarness.cpp` por responsabilidade antes de expandi-lo.
- [ ] Separar device/resources, scene generation, scenarios e metrics; cada arquivo <=300 linhas.
- [ ] Evoluir o mesmo runner por CLI, não criar outro D3D12 MiniGame.
- [ ] Separar `correctness` de `benchmark`; waits/Map do modo atual não entram no benchmark.
- [ ] Adicionar só cenários faltantes: steady/resize/reset/missing guides/provenance/on-off/failure.
- [ ] Padronizar saída p50/p95/p99 e counters com buffers pré-alocados.
- [ ] Instrumentação local mínima; sem tracer/framework grande.
- [ ] Qualquer test file >300 tocado nesta fase é dividido por cenário/subsistema.

## Revisão obrigatória

- [ ] Harness não replica policy/executor.
- [ ] Split preserva comportamento antes de adicionar features.
- [ ] Métrica exclui setup/waits/console.
- [ ] Nenhum CPU readback vira atalho de produto.
- [ ] Nenhum arquivo “helpers.cpp” vira depósito genérico.

## Validação rápida

- [ ] `nrfusion_sim` cobre loops CPU.
- [ ] Harness correctness cobre lifecycle/resources.
- [ ] Benchmark mede apenas trecho declarado.
- [ ] Relatório before/after automático.
- [ ] Checker confirma <=300 em todo código novo/tocado.

## Gate

- [ ] Loop implementar→executar→medir→revisar sem usuário.
- [ ] Correctness/performance separados.
- [ ] Harness existente ficou modular, não duplicado.
- [ ] Nenhum arquivo da infraestrutura de harness tocada >300 linhas.

## Próxima fase

Fase 04.
