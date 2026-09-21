# Fase 04 — NGX feature identity registry

## Objetivo

Identificar SR, RR, FG e unknown por lifecycle, nunca por heurística frágil.

## Dependências

Fase 03.

## Fora de escopo

- Executar NR
- Inferir tipo só por parâmetros

## Implementação

- [ ] Interceptar create/release relevantes.
- [ ] Registrar handle + kind + generation/context suficiente.
- [ ] Não registrar failed create.
- [ ] Remover no release e tratar pointer/handle reuse.
- [ ] Tratar recreate/resize e múltiplos viewports.
- [ ] Unknown passa intacto.
- [ ] FG nunca autoriza NR.

## Revisão obrigatória

- [ ] SR+FG e RR+FG na mesma sessão.
- [ ] Late release e reuse não herdam kind antigo.
- [ ] Registry lookup steady-state não aloca.
- [ ] Unknown runtime/version não cai em heurística silenciosa.

## Validação rápida

- [ ] Fake NGX client com create/release/reuse em massa.
- [ ] Falhas e orderings incomuns.
- [ ] Assertar que Evaluate(FG/Unknown) nunca chama NR.

## Gate

- [ ] FG→NR é estruturalmente impossível.
- [ ] Registry tem lifetime definido e custo bounded.

## Próxima fase

Fase 05.
