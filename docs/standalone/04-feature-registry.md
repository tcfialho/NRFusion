# Fase 04 — NGX feature identity registry

## Objetivo

Identificar SR, RR, FG e unknown por lifecycle com implementação pequena e bounded.

## Dependências

Fase 03.

## Fora de escopo

- Executar NR
- Inferir tipo por parâmetros

## Implementação

- [ ] Interceptar create/release relevantes.
- [ ] Registrar handle + kind + generation/context suficiente.
- [ ] Não registrar failed create.
- [ ] Tratar release/reuse/recreate/múltiplos viewports.
- [ ] Unknown passa intacto.
- [ ] FG nunca autoriza NR.
- [ ] Registry API e storage ficam separados se juntos se aproximarem de 250 linhas.

## Revisão obrigatória

- [ ] SR+FG e RR+FG.
- [ ] Late release/reuse não herdam kind.
- [ ] Lookup steady-state não aloca.
- [ ] Sem “utility” genérico crescendo para absorver hooks diversos.
- [ ] Arquivos <=300 linhas.

## Validação rápida

- [ ] Fake client com create/release/reuse em massa.
- [ ] Failure/orderings incomuns.
- [ ] Assert Evaluate(FG/Unknown) nunca chama NR.

## Gate

- [ ] FG→NR estruturalmente impossível.
- [ ] Lifetime/custo bounded.
- [ ] Nenhum arquivo novo/tocado >300 linhas.

## Próxima fase

Fase 05.
