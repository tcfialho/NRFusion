# Fase 21 — Otimização medida do executor

## Objetivo

Otimizar somente alvos medidos mantendo cada unidade pequena e revisável.

## Dependências

Fases 05,08,20.

## Fora de escopo

- Remover barrier por aparência
- Reescrever modelo NVIDIA
- Usar performance como desculpa para monólito

## Implementação

- [ ] Rankear copies/barriers/descriptors/readbacks/resolve por custo/frequência.
- [ ] Um alvo por iteração.
- [ ] Se o arquivo alvo legado ainda >300, fazer split mecânico e validar antes da otimização.
- [ ] Provar redundância/substituição segura.
- [ ] Preservar subrect/format/state/lifetime/failure.
- [ ] Medir before/after no mesmo cenário.
- [ ] Registrar quando não otimizar é a decisão correta.
- [ ] Nenhuma otimização pode ultrapassar cap de 300; extrair boundary real antes.

## Revisão obrigatória

- [ ] Barrier exige state proof.
- [ ] Copy exige equivalência.
- [ ] Não aumentar VRAM sem aprovação.
- [ ] Não agrupar mudanças não atribuíveis.
- [ ] Split e optimization ficam em commits distinguíveis quando possível.

## Validação rápida

- [ ] Scenario específico.
- [ ] Long-run regression.
- [ ] Hardware real para GPU/qualidade.
- [ ] LOC checker.

## Gate

- [ ] Ganho/simplificação demonstrável ou mudança descartada.
- [ ] Sem regressão funcional/VRAM.
- [ ] Todos arquivos alterados <=300 linhas.

## Próxima fase

Fase 22.
