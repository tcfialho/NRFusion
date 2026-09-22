# Fase 06 — NrSession — uma transação por frame

## Status

**Em andamento.** Subgate 06a conclui contrato/transação básica e decomposição inicial do runtime.

## Objetivo

Consolidar policy/runtime/telemetry sem criar novo God Object e reduzir dívida existente do core.

## Dependências

Fase 05.

## Fora de escopo

- Mudar policy por conveniência
- Otimizar GPU

## Implementação

- [x] Definir FramePacket/FrameResult mínimos.
- [x] Resolver scale/precision/placement/scheduler em uma transação.
- [ ] Integrar work identity/timing retirement.
- [ ] Remover OptiScalerAdapter e getters repetidos.
- [x] Separar RuntimeConfig de state mutável.
- [ ] Preservar stale timing/reconfigure quarantine; config-generation quarantine já implementada.
- [x] `NrSession` orquestra; helpers permanecem focados.
- [x] Antes de mudanças substanciais, decompor `FusionRuntime.hpp` (>300) em contratos/facades pequenos.
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


## Subgate 06a — contrato e configuração

- `NrSessionFramePacket` reúne game/frame/telemetry/capabilities e parâmetros de policy sem heap.
- `NrSessionFrameResult` devolve disposition + `AutoDecision` + gerações.
- `RuntimeConfig` permanece configuração; `NrSessionState` concentra state mutável.
- frame cuja `configurationGeneration` não coincide é rejeitado antes de `ResolveAuto`.
- reconfigure com generation antiga é rejeitado; generation nova abre novo runtime epoch.
- `BeginConfigurationEpoch` invalida state adaptativo antigo e avança a generation de precision.
- decisão válida é diferencialmente comparada com um `FusionRuntime` independente no teste portátil.
- `FusionRuntime.hpp` deixa de possuir helpers/identity de Auto; eles passam ao contrato focado
  `AutoDecision.hpp`, mantendo funções inline e sem custo de chamada adicional.

Próximo subgate: 06b — work identity/timing retirement com storage fixo e fake executor, removendo
o caminho por-frame de `OptiScalerAdapter` sem introduzir locks ou allocations steady.
