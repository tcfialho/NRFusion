# Fase 21 — Otimização medida do executor

## Objetivo

Reduzir custo GPU/CPU do executor somente quando existe evidência de um alvo concreto.

## Dependências

Fases 05,08,20.

## Fora de escopo

- Remover barrier por aparência
- Reescrever modelo NVIDIA
- Tradeoff visual implícito

## Implementação

- [ ] Rankear copies/barriers/descriptors/readbacks/resolve por custo ou frequência observada.
- [ ] Escolher um único alvo por iteração.
- [ ] Provar redundância ou substituição segura.
- [ ] Preservar subrect/format/state/lifetime/failure semantics.
- [ ] Medir antes/depois no mesmo cenário.
- [ ] Registrar quando 'não otimizar' é o resultado correto.

## Revisão obrigatória

- [ ] Barrier removido exige state proof completo.
- [ ] Copy removida exige equivalência de conteúdo e region.
- [ ] Não aumentar VRAM sem aprovação.
- [ ] Não agrupar mudanças que impedem atribuir resultado.

## Validação rápida

- [ ] Harness específico do caminho.
- [ ] Long-run regressions.
- [ ] Hardware real somente para conclusão sobre GPU time/qualidade.

## Gate

- [ ] Mudança tem ganho/simplificação demonstrável ou é descartada.
- [ ] Sem regressão funcional/VRAM conhecida.

## Próxima fase

Fase 22.
