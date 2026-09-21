# Fase 00 — Baseline e mapa de responsabilidades

## Status

**Concluída e revisada.** Evidência: [00-baseline-evidence.md](00-baseline-evidence.md).

## Objetivo

Congelar responsabilidades/comportamento e instalar os gates estruturais antes de mover código.

## Implementação

- [x] Congelar master/upstreams.
- [x] Mapear responsabilidades do host OptiScaler.
- [x] Inventariar recursos/timing/lifetimes relevantes.
- [x] Registrar capabilities e limitações atuais.
- [x] Inventariar os 34 arquivos first-party >300 linhas e seus owners.
- [x] Criar checker `changed/all`.
- [x] Fazer code review e corrigir falsos passes/falsos bloqueios do checker.
- [x] Preservar regressões específicas do checker.

## Gate

- [x] Nenhuma responsabilidade crítica sem owner.
- [x] Todo oversized first-party file tem destino.
- [x] Checker cobre source novo/tocado, untracked e vendor modificado.
- [x] Exceções generated/vendor têm regra mecânica.
- [x] Baseline permite comparação diferencial.

## Próxima fase

Fase 01 — Universal FrameContract.
