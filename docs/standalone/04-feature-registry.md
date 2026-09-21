# Fase 04 — NGX feature identity registry

## Status

**Concluída.** Evidência: [04-feature-registry-evidence.md](04-feature-registry-evidence.md).

## Objetivo

Identificar SR, RR, FG e unknown por lifecycle com implementação pequena e bounded.

## Dependências

Fase 03 concluída e revisada.

## Fora de escopo

- Executar NR
- Inferir tipo por parâmetros
- Instalar detours NGX antes do carrier que possui esse boundary

## Auditoria inicial

- [x] Nenhum registry first-party NGX existia.
- [x] Create/Release/Evaluate existentes estavam no tool Requiem ou fixture OptiScaler.
- [x] Core standalone não dependia de headers NGX/vendor.
- [x] Fase 05 possui feature lifecycle; Fase 07 é dona da integração do registry no carrier.

## Implementação

- [x] Receber lifecycle create/release pelo registry boundary.
- [x] Classificar feature ID bruto em SR/RR/FG/Unknown sem parâmetros.
- [x] Registrar somente create bem-sucedido.
- [x] Registrar handle + kind + context + generation.
- [x] Tratar release/reuse/recreate e mesmo handle em múltiplos contextos.
- [x] Exigir token de geração no release para rejeitar release tardio.
- [x] Preservar Unknown sem inferência.
- [x] Fazer FG e Unknown resultarem somente em passthrough.
- [x] Usar storage fixo de 64 slots, sem heap ou lock.
- [x] Manter API/storage abaixo de 250 linhas separadamente.

## Revisão obrigatória

- [x] SR+FG e RR+FG.
- [x] Late release/reuse não herdam kind.
- [x] Lookup steady-state não aloca.
- [x] Sem utility genérico absorvendo hooks diversos.
- [x] Arquivos <=300 linhas.

## Validação rápida

- [x] Fake client com 100.000 create/release/reuse.
- [x] Failed create/orderings incomuns.
- [x] Mesmo raw handle em contextos diferentes.
- [x] Capacity overflow fail-closed.
- [x] FG/Unknown nunca autorizam NR.
- [x] 1.000.000 lookups com 0 allocations no trecho medido.
- [x] Portable Core 6/6 PASS.
- [x] Windows CTest 19/19 PASS.
- [x] Windows integrated validation PASS.

## Gate

- [x] FG→NR impossível pela decisão do registry.
- [x] Lifetime/custo bounded: 64 slots, scan máximo de 64, generation monotônica.
- [x] Nenhum arquivo novo/tocado >300 linhas.

## Próxima fase

Fase 05 — somente após instrução explícita do usuário.
