# Fase 23 — A/B final e cutover do OptiScaler

## Objetivo

Trocar o host principal somente após equivalência, performance e rollback claros.

## Dependências

Fase 22.

## Fora de escopo

- Apagar fallback cedo
- Claim de GPU sem hardware real

## Implementação

- [ ] Comparar CPU p50/p95/p99, allocations, locks, resource creates, queries/copies.
- [ ] Comparar resource ledger e peak VRAM real.
- [ ] Executar A/B real: GPU frame ms, NR ms, FPS, 1% low, p95/p99 e MFG pacing.
- [ ] Validar menu/config/recovery e rotas publicamente anunciadas.
- [ ] Manter OptiScaler como referência/fallback durante preview.
- [ ] Definir critérios objetivos de rollback.
- [ ] Só remover dependência primária após uma release de transição estável.

## Revisão obrigatória

- [ ] A/B usa feature/configuração equivalente.
- [ ] Warm-up/cena/carga comparáveis.
- [ ] Resultado negativo é blocker ou limitação explícita.
- [ ] Cutover não depende de um único jogo/API.

## Validação rápida

- [ ] Harness suite completa antes de cada RC.
- [ ] Hardware real nas rotas prioritárias.
- [ ] Stress resize/reset/reconfigure/MFG.

## Gate

- [ ] D3D12 x64 Hardware-qualified.
- [ ] FrameContract/registry/Host64 estáveis.
- [ ] Rotas anunciadas ao usuário estão Hardware-qualified; demais aparecem como Target/Blocked.
- [ ] 0 steady-state allocation/resource creation no normal path.
- [ ] VRAM equivalente sem regressão.
- [ ] Host CPU abaixo do baseline equivalente.
- [ ] Rollback testado.

## Próxima fase

Fim.
