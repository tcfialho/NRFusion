# Fase 21 — Otimização medida do executor

## Objetivo

Otimizar alvos medidos após decompor qualquer unidade oversized ativa.

## Dependências

Fases 05,08,20.

## Fora de escopo

- Barrier por aparência
- Reescrever modelo
- Performance como desculpa para monólito

## Implementação

- [ ] Rankear copies/barriers/descriptors/readbacks/resolve.
- [ ] Um alvo por iteração.
- [ ] Se ativos, decompor `AdaW4A8Interceptor.cpp` por cache/capability/interception/status e `W4A8FfnSm89.cu` por kernel family antes de evoluir.
- [ ] Separar benchmark CUDA ativo se ainda >300.
- [ ] Provar redundância/substituição.
- [ ] Preservar state/lifetime/failure.
- [ ] Medir before/after.
- [ ] Registrar quando não otimizar é correto.

## Revisão obrigatória

- [ ] Barrier exige state proof.
- [ ] Copy exige equivalência.
- [ ] Não aumentar VRAM sem aprovação.
- [ ] Split mecânico e optimization ficam distinguíveis.
- [ ] Split não piora p95/p99 por perda de inlining ou nova indireção; se piorar, redesenhar boundary.
- [ ] Kernel split não duplica constants/layout logic.

## Validação rápida

- [ ] Scenario específico.
- [ ] Long-run regression.
- [ ] Hardware real para GPU/quality.
- [ ] LOC checker.

## Gate

- [ ] Ganho demonstrável ou mudança descartada.
- [ ] Sem regressão.
- [ ] Todos arquivos ativos <=300.

## Próxima fase

Fase 22.
