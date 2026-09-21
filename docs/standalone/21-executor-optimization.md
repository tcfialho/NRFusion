# Fase 21 — Otimização medida do executor

## Objetivo

Otimizar alvos medidos após decompor unidades oversized sem alterar a forma do hot path por conveniência.

## Dependências

Fases 05,08,20.

## Fora de escopo

- Barrier por aparência
- Reescrever modelo
- Performance como desculpa para monólito

## Implementação

- [ ] Rankear copies/barriers/descriptors/readbacks/resolve.
- [ ] Um alvo por iteração.
- [ ] `AdaW4A8Interceptor.cpp`: separar cache/persistence, capability/module discovery, launch interception e status/publication.
- [ ] `W4A8FfnSm89.cu`: separar device helpers compartilhados, TensorCore family, DP4A family e host launch/selection sem duplicar layout/constants.
- [ ] Benchmark CUDA ativo fica em unidade própria <=300.
- [ ] Provar redundância/substituição.
- [ ] Preservar state/lifetime/failure.
- [ ] Medir before/after; split mecânico precede optimization.
- [ ] Registrar quando não otimizar é correto.

## Revisão obrigatória

- [ ] Barrier exige state proof.
- [ ] Copy exige equivalência.
- [ ] Não aumentar VRAM sem aprovação.
- [ ] Split não piora p95/p99 por inlining/indireção.
- [ ] Kernel split não duplica constants, device state ou quantization layout.
- [ ] Helper `.cuh` também respeita 300 linhas.

## Validação rápida

- [ ] Scenario específico.
- [ ] Long-run regression.
- [ ] Hardware real para GPU/quality.
- [ ] LOC checker.
- [ ] CUDA split: benchmark antes/depois mesmo sem mudança algorítmica.

## Gate

- [ ] Ganho demonstrável ou mudança descartada.
- [ ] Sem regressão.
- [ ] Todos arquivos ativos <=300.

## Próxima fase

Fase 22.
