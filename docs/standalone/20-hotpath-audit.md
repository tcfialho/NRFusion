# Fase 20 — Auditoria de hot path e estrutura

## Objetivo

Provar performance e eliminar toda dívida estrutural que não será aposentada.

## Dependências

Rotas principais implementadas.

## Fora de escopo

- Correctness harness como benchmark
- Split estético sem boundary

## Implementação

- [ ] Auditar allocations, containers, formatting, filesystem, scans, locks, creates e waits.
- [ ] Auditar descriptors, queries, copies e capability/config queries.
- [ ] Classificar init/reconfigure/steady.
- [ ] Contar custo/frame.
- [ ] Rodar checker sobre **todo** first-party handwritten code.
- [ ] Checker usa allowlist só para generated/vendor/upstream fixtures.
- [ ] Resolver dívida ativa conhecida: controller tests, PerformanceController, ProfileStore, Ada interceptor, W4A8 CUDA/tools e demais itens do relatório.
- [ ] Testes grandes são divididos por subsystem/scenario; CUDA por kernel/responsabilidade; tools por etapa de trabalho.
- [ ] Código marcado para retirement não é refatorado se sua remoção ocorrer antes da RC.
- [ ] Checker permanece pequeno; enforcement não vira framework.

## Revisão obrigatória

- [ ] Todo steady cost justificado.
- [ ] Benchmark exclui waits/Map/console.
- [ ] Warm-up separado; p50/p95/p99.
- [ ] Tail regression bloqueia.
- [ ] Nenhuma allowlist criada para escapar do cap.
- [ ] Minificação não é solução.

## Validação rápida

- [ ] CPU/fake long run.
- [ ] Benchmark carriers.
- [ ] Correctness separado.
- [ ] Relatório LOC completo.

## Gate

- [ ] Performance contract cumprido.
- [ ] Arquivos novos/tocados: zero >300.
- [ ] Todo legado >300 restante está explicitamente marcado para retirement antes da RC.

## Próxima fase

Fase 21.
