# Fase 06 — NrSession — uma transação por frame

## Objetivo

Consolidar policy/runtime/telemetry em um frame transaction e remover sincronização repetida.

## Dependências

Fase 05.

## Fora de escopo

- Mudar policy por conveniência
- Otimizar GPU

## Implementação

- [ ] Definir FramePacket/FrameResult mínimos.
- [ ] Resolver scale, precision, placement e scheduler em uma transação.
- [ ] Integrar work identity e retirement de timing.
- [ ] Remover getters/adapters repetidos do frame path.
- [ ] Separar RuntimeConfig snapshot de state mutável.
- [ ] Preservar stale timing, generation e quarantine de reconfigure.
- [ ] Definir owner thread de cada estado; atomics apenas onde há leitor concorrente.

## Revisão obrigatória

- [ ] Old call graph vs new call graph.
- [ ] Cada lock removido/adicionado tem concorrência demonstrada.
- [ ] Nenhum estado possui dois owners implícitos.
- [ ] Transação não vira monólito: helpers puros permanecem testáveis.

## Validação rápida

- [ ] Fake executor por milhões de frames.
- [ ] Differential de decisões contra caminho atual quando possível.
- [ ] Timing atrasado, reset, overload e config generation change.

## Gate

- [ ] 0 heap allocations steady-state.
- [ ] Menos locks/chamadas intermediárias.
- [ ] Mesmas decisões para entradas equivalentes.

## Próxima fase

Fase 07.
