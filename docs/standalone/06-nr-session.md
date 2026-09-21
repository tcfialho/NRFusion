# Fase 06 — NrSession — uma transação por frame

## Objetivo

Consolidar policy/runtime/telemetry sem criar um novo God Object.

## Dependências

Fase 05.

## Fora de escopo

- Mudar policy por conveniência
- Otimizar GPU

## Implementação

- [ ] Definir FramePacket/FrameResult mínimos.
- [ ] Resolver scale/precision/placement/scheduler em uma transação.
- [ ] Integrar work identity/timing retirement.
- [ ] Remover getters/adapters repetidos.
- [ ] Separar RuntimeConfig snapshot de state mutável.
- [ ] Preservar stale timing/generation/reconfigure quarantine.
- [ ] Definir owner thread de cada state.
- [ ] `NrSession` orquestra; policy helpers permanecem módulos pequenos e focados.
- [ ] Header contém contrato, não implementação para “economizar” linhas do .cpp.

## Revisão obrigatória

- [ ] Old vs new call graph.
- [ ] Cada lock tem concorrência demonstrada.
- [ ] Nenhum state tem dois owners.
- [ ] Nenhum helper é extraído só para burlar LOC.
- [ ] Cada arquivo <=300 linhas.

## Validação rápida

- [ ] Fake executor por milhões de frames.
- [ ] Differential de decisões.
- [ ] Timing atrasado/reset/overload/config generation.
- [ ] Checker de tamanho.

## Gate

- [ ] 0 heap allocations steady.
- [ ] Menos locks/chamadas.
- [ ] Mesmas decisões para entradas equivalentes.
- [ ] NrSession e helpers <=300 linhas por arquivo.

## Próxima fase

Fase 07.
