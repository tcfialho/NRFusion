# Fase 04 — NGX feature identity registry

## Status

**Concluída após revisão adversarial.**
Evidência: [04-feature-registry-evidence.md](04-feature-registry-evidence.md).

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
- [x] Rejeitar create duplicado enquanto a identidade anterior ainda está ativa.
- [x] Tratar release→reuse/recreate e mesmo handle em múltiplos contextos.
- [x] Exigir token de geração no release interno para rejeitar evento stale.
- [x] Preservar Unknown sem inferência.
- [x] Fazer FG, Unknown, missing e raw handle ambíguo resultarem somente em passthrough.
- [x] Oferecer lookup/action por raw handle somente quando ele é único.
- [x] Usar storage fixo de 64 slots, sem heap ou lock.
- [x] Manter API/storage abaixo de 250 linhas separadamente.

## Contrato com o ABI NGX

- [x] Feature IDs verificados no enum público: SR=1, FG=11, RR=13.
- [x] Evaluate pode usar raw-handle lookup fail-closed quando contexto não estiver disponível.
- [x] Raw handle presente em múltiplos contextos é ambíguo e retorna passthrough.
- [x] Feature privada 18 usada pelo NR existente permanece Unknown/PassThrough.
- [x] Release NGX expõe somente raw handle; generation não pode ser reconstruída pelo registry.
- [x] Carrier futuro deve reter o token retornado no create junto do owner/wrapper da feature.
- [x] Se o owner perder essa associação, release generation-safe deve falhar fechado.

## Revisão obrigatória

- [x] SR+FG e RR+FG.
- [x] Release→reuse não herda kind.
- [x] Stale token não remove geração nova.
- [x] Lookup steady-state não aloca.
- [x] Sem utility genérico absorvendo hooks diversos.
- [x] Arquivos <=300 linhas.

## Validação rápida

- [x] Fake client com 100.000 create/release/reuse.
- [x] Failed create/orderings incomuns.
- [x] Mesmo raw handle em contextos diferentes.
- [x] Raw lookup ambíguo fail-closed.
- [x] Capacity overflow fail-closed.
- [x] FG/Unknown/feature 18 nunca autorizam NR.
- [x] 1.000.000 lookups com 0 allocations, incluindo raw-handle path.
- [x] Portable Core 6/6 PASS.
- [x] Windows CTest 19/19 PASS.
- [x] Windows integrated validation PASS.

## Gate

- [x] Registry nunca retorna NR para FG/Unknown/missing/ambiguous.
- [x] Lifetime/custo bounded: 64 slots, scan máximo de 64, generation monotônica.
- [x] Lifecycle ativo duplicado falha fechado em vez de sobrescrever identidade.
- [x] Nenhum arquivo novo/tocado >300 linhas.
- [x] Limitação do raw ReleaseFeature documentada como requisito do carrier.

## Próxima fase

Fase 05 — somente após instrução explícita do usuário.
