# Fase 20 — Auditoria de hot path e estrutura

## Objetivo

Provar performance e eliminar dívida estrutural sem fragmentar runtime ou CI artificialmente.

## Dependências

Rotas principais implementadas.

## Fora de escopo

- Correctness harness como benchmark
- Split estético sem boundary

## Implementação

- [ ] Auditar allocations, containers, formatting, filesystem, scans, locks, creates e waits.
- [ ] Auditar descriptors, queries, copies e capability/config queries.
- [ ] Classificar init/reconfigure/steady e contar custo/frame.
- [ ] Rodar checker sobre todo first-party handwritten code.
- [ ] Resolver dívida ativa: controller tests, PerformanceController, ProfileStore, Ada interceptor, W4A8 CUDA/tools e relatório restante.
- [ ] `controller_tests.cpp`: dividir por subsystem/scenario em vários .cpp ligados ao **mesmo** test executable, com um main pequeno; não criar vários jobs.
- [ ] Test utilities compartilhadas ficam pequenas; evitar novo `TestHelpers.cpp` monolítico.
- [ ] CUDA divide por kernel family/responsabilidade; tools por etapa.
- [ ] Código a aposentar não é refatorado se sair antes da RC.
- [ ] Checker permanece pequeno/independente de build completo.

## Revisão obrigatória

- [ ] Todo steady cost justificado.
- [ ] Benchmark exclui waits/Map/console.
- [ ] Warm-up separado; p50/p95/p99.
- [ ] Tail regression bloqueia.
- [ ] Nenhuma exclusão criada para escapar do cap.
- [ ] Split não introduz virtual/heap/lock/indireção sem razão.
- [ ] Mais arquivos não significam mais executáveis, jobs ou builds completos.

## Validação rápida

- [ ] CPU/fake long run.
- [ ] Benchmark carriers.
- [ ] Correctness separado.
- [ ] Relatório LOC vs baseline.
- [ ] Hot translation-unit split: benchmark before/after.

## Gate

- [ ] Performance contract cumprido.
- [ ] Arquivos novos/tocados: zero >300.
- [ ] Legado >300 restante está marcado para retirement antes da RC.
- [ ] Estrutura de tests/build não aumentou CI desnecessariamente.

## Próxima fase

Fase 21.
