# Fase 06 — NrSession — uma transação por frame

## Objetivo

Consolidar policy/runtime/telemetry sem criar novo God Object e reduzir dívida existente do core.

## Dependências

Fase 05.

## Fora de escopo

- Mudar policy por conveniência
- Otimizar GPU

## Implementação

- [ ] Definir FramePacket/FrameResult mínimos.
- [ ] Resolver scale/precision/placement/scheduler em uma transação.
- [ ] Integrar work identity/timing retirement.
- [ ] Remover OptiScalerAdapter e getters repetidos.
- [ ] Separar RuntimeConfig de state mutável.
- [ ] Preservar stale timing/generation/reconfigure quarantine.
- [ ] `NrSession` orquestra; helpers permanecem focados.
- [ ] Antes de mudanças substanciais, decompor `FusionRuntime.hpp` (>300) em contratos/facades pequenos.
- [ ] Se `PerformanceController.cpp` for evoluído, separar estimativa/cost learning da state machine de escala.
- [ ] `ProfileStore.cpp` só é tocado após separar codec/validation de persistence, se ainda fizer parte do standalone.

## Revisão obrigatória

- [ ] Old vs new call graph.
- [ ] Cada lock tem concorrência demonstrada.
- [ ] Nenhum state tem dois owners.
- [ ] Header contém contrato, não implementação escondida.
- [ ] Split não duplica policy.

## Validação rápida

- [ ] Fake executor milhões de frames.
- [ ] Differential decisions.
- [ ] Timing/reset/overload/config changes.
- [ ] LOC checker.

## Gate

- [ ] 0 heap allocations steady.
- [ ] Menos locks/calls.
- [ ] Mesmas decisões equivalentes.
- [ ] Core tocado <=300 linhas por arquivo.

## Próxima fase

Fase 07.
