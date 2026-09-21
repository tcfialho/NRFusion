# Fase 21 — Otimização medida do executor

## Objetivo

Otimizar GPU plumbing apenas com custo concreto demonstrado.

## Dependências

Fases 05,08,20.

## Fora de escopo

- Remover barrier por aparência
- Reescrever modelo
- Trocar qualidade sem evidência

## Checklist de implementação

- [ ] Medir copies/barriers/descriptors/readbacks/resolve.
- [ ] Escolher um custo por iteração.
- [ ] Provar redundância/substituição.
- [ ] Preservar states/lifetime/errors.
- [ ] Alterar mínimo.
- [ ] Harness before/after.
- [ ] Hardware real quando dependente de GPU/driver.

## Revisão obrigatória

- [ ] Barrier exige state proof.
- [ ] Copy exige subrect/padding/format preservation.
- [ ] Não aumentar VRAM sem aprovação.
- [ ] Não agrupar otimizações não atribuíveis.

## Validação rápida

- [ ] Scenario específico.
- [ ] Long-run regression.
- [ ] GPU A/B quando necessário.

## Gate de conclusão

- [ ] Ganho/simplificação mensurável.
- [ ] Sem regressão funcional/VRAM.

## Entregáveis

- Optimization notes
- Before/after

## Próxima fase

Fase 22.
