# Fase 20 — Auditoria rígida de hot path

## Objetivo

Provar por revisão e harness que o standalone é menor e mais previsível que o host atual.

## Dependências

Rotas principais implementadas.

## Fora de escopo

- Microbenchmark irrelevante ao frame path
- Otimização especulativa do modelo

## Implementação

- [ ] Auditar new/delete, vector/string growth, map insertion, std::function e formatting.
- [ ] Auditar filesystem/module/PE scans.
- [ ] Auditar mutex/shared_mutex/atomics fortes sem necessidade.
- [ ] Auditar CreateResource/Heap/PSO e waits síncronos.
- [ ] Auditar capability/timestamp-frequency/config conversions repetidas.
- [ ] Classificar cada ocorrência como init, reconfigure ou steady-state.
- [ ] Gerar contagem normal por frame: locks, creates, queries, copies e host calls.

## Revisão obrigatória

- [ ] Todo custo steady-state restante precisa de justificativa concreta.
- [ ] Benchmark separa warm-up e mede p50/p95/p99.
- [ ] Regressão de tail latency bloqueia merge até explicada.
- [ ] Comparação com OptiScaler usa trabalho equivalente.

## Validação rápida

- [ ] Fake executor milhões de frames.
- [ ] Harness de cada carrier principal em long run.
- [ ] Before/after de cada remoção relevante.

## Gate

- [ ] 0 heap allocation normal.
- [ ] 0 resource/heap/PSO creation normal.
- [ ] 0 filesystem/string/config parsing normal.
- [ ] 0 blocking wait normal.
- [ ] Locks inevitáveis documentados.
- [ ] CPU e p99 abaixo do baseline equivalente ou blocker explícito.

## Próxima fase

Fase 21.
