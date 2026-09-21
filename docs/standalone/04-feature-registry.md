# Fase 04 — NGX feature identity registry

## Status

**Em andamento.**

## Objetivo

Identificar SR, RR, FG e unknown por lifecycle com implementação pequena e bounded.

## Dependências

Fase 03 concluída e revisada.

## Fora de escopo

- Executar NR
- Inferir tipo por parâmetros
- Instalar hooks NGX reais antes das fases de carrier/executor

## Auditoria inicial — 2026-09-21

- [x] Nenhum registry first-party NGX existe hoje.
- [x] Create/Release/Evaluate existentes estão no tool Requiem ou fixture OptiScaler.
- [x] Core standalone não depende de headers NGX/vendor.
- [x] Fase 04 pode permanecer portátil/CPU.
- [x] Release tardio exige token de geração; lookup só por handle não é suficiente após reuse.
- [x] Mesmo handle em contextos diferentes precisa permanecer independente.

## Implementação

- [ ] Registrar create bem-sucedido com handle + kind + context + generation.
- [ ] Não registrar failed create.
- [ ] Tratar release/reuse/recreate/múltiplos contextos.
- [ ] Preservar Unknown sem inferência.
- [ ] Fazer FG e Unknown resultarem somente em passthrough.
- [ ] Manter API e storage pequenos e separados por responsabilidade.
- [ ] Usar storage fixo sem heap.

## Revisão obrigatória

- [ ] SR+FG e RR+FG.
- [ ] Late release/reuse não herdam kind.
- [ ] Lookup steady-state não aloca.
- [ ] Sem utility genérico absorvendo hooks diversos.
- [ ] Arquivos <=300 linhas.

## Validação rápida

- [ ] Fake client com create/release/reuse em massa.
- [ ] Failure/orderings incomuns.
- [ ] Assert evaluate de FG/Unknown nunca autoriza NR.
- [ ] Capacity/fail-closed.
- [ ] Lookup massivo com contador de allocations.

## Gate

- [ ] FG→NR estruturalmente impossível.
- [ ] Lifetime/custo bounded.
- [ ] Nenhum arquivo novo/tocado >300 linhas.

## Próxima fase

Fase 05 somente após fechamento e revisão da Fase 04.
