# Fase 06 — NrSession — uma transação por frame

## Objetivo

Unificar policy/runtime/telemetry e remover fragmentação/locks do adapter.

## Dependências

Fase 05.

## Fora de escopo

- Alterar policy sem motivo
- Otimizar GPU

## Checklist de implementação

- [ ] Definir FramePacket/FrameResult.
- [ ] Consolidar scale/precision/placement/scheduler.
- [ ] Consolidar work identity/timing retirement.
- [ ] Remover getters repetidos.
- [ ] Remover entradas repetidas no mesmo mutex.
- [ ] Preservar stale timing/scale generation/reconfigure quarantine.
- [ ] Separar config snapshot de state mutável.
- [ ] Definir owner thread.

## Revisão obrigatória

- [ ] Old call graph vs new transaction.
- [ ] Cada lock removido com análise de concorrência.
- [ ] Cada state removido com novo owner.
- [ ] Evitar monólito não revisável.

## Validação rápida

- [ ] Fake executor por milhões de frames.
- [ ] Benchmark old adapter vs NrSession quando possível.
- [ ] Timing atrasado/reset/reconfigure/overload.

## Gate de conclusão

- [ ] Menos locks/calls.
- [ ] Mesmo resultado lógico.
- [ ] 0 heap allocations steady.

## Entregáveis

- NrSession
- Before/after call-path

## Próxima fase

Fase 07.
