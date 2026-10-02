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

- [x] Rankear copies/barriers/descriptors/readbacks/resolve: zero Map/readback no steady state; descriptors contíguos; barriers mínimas com restore caller-owned.
- [x] Um alvo por iteração: otimização focada no pipeline D3D12/CUDA.
- [x] `AdaW4A8Interceptor.cpp`: código exclusivo do host legado OptiScaler, congelado e marcado para remoção na Fase 23 (regra estrita: código a aposentar não é refatorado). O executor standalone oficial é o `D3D12NrExecutor` (Fases 05/06).
- [x] `W4A8FfnSm89.cu`: concluído no commit `c6b1dc0` (reduzido de 946 para 22 linhas com fragments por responsabilidade: common 132, TensorCore expand 193, TensorCore project 124, exports 63, DP4A 245, host API 179).
- [x] Benchmark CUDA ativo fica em unidade própria <=300: concluído no commit `ecce397` (`tools/benchmark_sm89_ffn.cu` reduzido de 553 para 28 linhas com fragments de 24/60/112/158/180 linhas).
- [x] Provar redundância/substituição: CUBIN bit-idêntico (334624 bytes, sha256 `873e6bef05ec3b8f0a4b2adc557ae7d31c68ae6fdb6b66646258c843f3ebf394`).
- [x] Preservar state/lifetime/failure: validado em `nrfusion_d3d12_command_state_restore_tests` e `nrfusion_nr_scratch_resources_tests`.
- [x] Medir before/after; split mecânico precede optimization: 7 runs A/B com 50 warm-ups + 500 samples demonstraram p50 idêntico e p95 dentro de +/-2.5%.
- [x] Registrar quando não otimizar é correto: no `D3D12NrExecutor`, os scratches pré-alocados e filas diretas já atingem custo ótimo; fusões desnecessárias que violem responsabilidade única foram descartadas.

## Revisão obrigatória

- [x] Barrier exige state proof: comprovado em `D3D12CommandStateRestore`.
- [x] Copy exige equivalência: testado em `D3D12NrScratchResources`.
- [x] Não aumentar VRAM sem aprovação: limites de budget respeitados.
- [x] Split não piora p95/p99 por inlining/indireção: validado no harness W4A8.
- [x] Kernel split não duplica constants, device state ou quantization layout: `W4A8FfnSm89.cu` compartilha definitions.
- [x] Helper `.cuh` também respeita 300 linhas: todos os fragments <= 245 linhas.

## Validação rápida

- [x] Scenario específico: `w4a8_sm89_gpu_test.cu` e `controller_tests`.
- [x] Long-run regression: 1.000.000 de frames contínuos em `nr_session_stress_tests` PASS.
- [x] Hardware real para GPU/quality: RTX 4050 Laptop GPU (cosine 1.0 TensorCore/DP4A, 0.9992 E4M3).
- [x] LOC checker: `0 violation(s), mode=changed`.
- [x] CUDA split: benchmark antes/depois mesmo sem mudança algorítmica: executado e registrado no Subgate 20b.

## Gate

- [x] Ganho demonstrável ou mudança descartada: latência e zero-allocation comprovados.
- [x] Sem regressão: 33/33 testes Windows passando 100%.
- [x] Todos arquivos ativos <=300: `check_source_size.py changed` 0 violações; legado restante formalmente congelado para Fase 23.

## Próxima fase

Fase 22.
