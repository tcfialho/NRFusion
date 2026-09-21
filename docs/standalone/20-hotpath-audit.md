# Fase 20 — Auditoria rígida de hot path

## Objetivo

Provar por revisão e medição isolada que o standalone é menor e mais previsível que o host atual.

## Dependências

Rotas principais implementadas.

## Fora de escopo

- Tratar o tempo total do correctness harness como overhead do NRFusion
- Otimização especulativa do modelo

## Implementação

- [ ] Auditar new/delete, vector/string growth, map insertion, std::function e formatting.
- [ ] Auditar filesystem/module/PE scans.
- [ ] Auditar mutex/shared_mutex e atomics fortes sem necessidade.
- [ ] Auditar CreateResource/Heap/PSO e waits síncronos.
- [ ] Auditar descriptor writes/frame separadamente de heap/resource allocation.
- [ ] Auditar capability/timestamp-frequency/config conversions repetidas.
- [ ] Revisar locks dos SyntheticDx12/Dx11/OpenGL providers.
- [ ] Classificar cada custo como init, reconfigure ou steady-state.
- [ ] Contar por frame: locks, creates, descriptor writes, queries, copies e host calls.

## Revisão obrigatória

- [ ] Todo custo steady-state restante tem justificativa concreta.
- [ ] Descriptor write pode ser aceitável; descriptor heap creation/frame não.
- [ ] Benchmark exclui fence waits, GPU Map/Unmap e console usados apenas pelo harness de correção.
- [ ] Warm-up é separado; reportar p50/p95/p99.
- [ ] Regressão de tail latency bloqueia merge até explicada.
- [ ] Comparação OptiScaler usa trabalho equivalente.

## Validação rápida

- [ ] CPU/fake paths por milhões de frames.
- [ ] Benchmark mode dos carriers principais.
- [ ] Correctness mode separado para sync/resources.
- [ ] Before/after de cada remoção relevante.

## Gate

- [ ] 0 heap allocation normal.
- [ ] 0 GPU resource/heap/PSO creation normal.
- [ ] 0 filesystem/string/config parsing normal.
- [ ] 0 blocking wait normal.
- [ ] Locks restantes documentados e medidos.
- [ ] CPU/p99 abaixo do baseline equivalente ou blocker explícito.

## Próxima fase

Fase 21.
