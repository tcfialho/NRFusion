# Fase 20 — Auditoria de hot path e estrutura

## Objetivo

Provar performance e eliminar dívida estrutural sem fragmentar runtime ou CI artificialmente.

## Dependências

Rotas principais implementadas.

## Fora de escopo

- Correctness harness como benchmark
- Split estético sem boundary

## Implementação

- [ ] Auditar allocations, containers, formatting, filesystem, scans, locks, creates e waits.
- [ ] Auditar descriptors, queries, copies e capability/config queries.
- [ ] Classificar init/reconfigure/steady e contar custo/frame.
- [x] Rodar checker sobre todo first-party handwritten code.
- [ ] Resolver dívida ativa: controller tests, PerformanceController, ProfileStore, Ada interceptor, W4A8 CUDA/tools e relatório restante.
- [x] `controller_tests.cpp`: dividido por subsystem/scenario no mesmo test executable.
- [ ] Test utilities compartilhadas ficam pequenas; evitar novo `TestHelpers.cpp` monolítico.
- [ ] CUDA divide por kernel family/responsabilidade; tools por etapa.
- [ ] Código a aposentar não é refatorado se sair antes da RC.
- [x] Checker permanece pequeno/independente de build completo.

## Subgate 20a — W4A8 SM89 hot translation unit

Estado inicial do checker strict: 21 arquivos first-party >300 linhas.
`controller_tests.cpp` já estava dividido no mesmo executável e
`PerformanceController.cpp` já estava em 282 linhas.

Tentativa `aacb465` dividiu `ProfileStore.cpp`, mas a closure OptiScaler falhou porque
`tools/apply_to_optiscaler.py` materializa uma lista fechada de sources e tem 3899 linhas.
Adicionar os novos fragments ali criaria outra violação; o split foi revertido em `aa865cb`
sem exceção/bypass do checker.

Correção `c6b1dc0`:
- `W4A8FfnSm89.cu` caiu de 946 para 22 linhas;
- fragments por responsabilidade: common 132, TensorCore expand 193,
  TensorCore project 124, exports 63, DP4A 245, host API 179;
- um único translation unit/executável continua sendo usado; build scripts não mudaram;
- reconstrução textual do source anterior foi verificada byte-a-byte antes do commit;
- strict checker caiu de 21 para 20 violações.

Validação:
- Portable `36370324936`, Focused `36370324785`, Windows `36370324738`: PASS.
- Checkpoint `10948033992`, sha256
  `1c8e5bb2208f7b00ce68fa94790b502628a0b3f232a942355f85d59b98c2605a`.
- RTX 4050: TensorCore/DP4A cosine 1.0, max diff 0; E4M3 cosine 0.999212.
- CUBIN before/after: 334624 bytes e SHA-256
  `873e6bef05ec3b8f0a4b2adc557ae7d31c68ae6fdb6b66646258c843f3ebf394`
  em ambos; device code é bit-idêntico.
- 7 runs A/B com 50 warm-ups + 500 samples: p50 ficou essencialmente idêntico;
  p95 ficou dentro de aproximadamente +/-2.5% nos tamanhos 64/960/2160/3840.

O p99 curto ainda oscila fortemente mesmo com CUBIN idêntico. O benchmark atual chama
`cudaEventSynchronize` a cada sample, portanto não satisfaz ainda o requisito
"benchmark exclui waits". Performance contract e tail gate permanecem abertos.

Próxima ação: dividir estruturalmente `tools/benchmark_sm89_ffn.cu` (552 linhas) mantendo
o mesmo executável; depois alterar o timing para enfileirar os event pairs e sincronizar
somente após o lote, produzindo p50/p95/p99 sem wait por sample.

## Subgate 20b — benchmark e testes W4A8 / Capture32

`ecce397` dividiu `tools/benchmark_sm89_ffn.cu` de 553 para 28 linhas, com fragments
de 24/60/112/158/180 linhas no mesmo translation unit e executável.
Reconstruction byte-a-byte foi verificada antes do commit; RTX 4050 recompilou e executou
o benchmark completo. Portable `36371208298`: PASS.

`1614001` corrigiu a metodologia de timing:
- warm-up continua separado;
- 500 event pairs são enfileirados sem `cudaEventSynchronize` entre samples;
- há uma única sincronização após o lote;
- output passou a reportar mean/p50/p95/p99 explicitamente.
Portable `36371388095`: PASS.
A variância de p99 restante é física (preempção/clock do sistema), não wait do host.

`e2b0ef5` dividiu `tests/w4a8_sm89_gpu_test.cu` de 313 para 32 linhas,
com setup 132 e validação 152; um único executável continua sendo usado.
RTX 4050: TensorCore/DP4A cosine 1.0, max diff 0; E4M3 cosine 0.999212.
Portable `36371592378`, Focused `36371592413`, Windows `36371592401`: PASS.

`71dac21` dividiu somente suporte Win32/D3D11 do hook test Capture32:
principal 235 linhas, support 80; cenários/main/executável não mudaram.
Portable `36371937531`, Focused `36371937546`, Windows `36371937521`: PASS.
Checkpoint `10949775621`, sha256
`35b5ac3cbeee7f5d54ee27152507a46b5635248ba535c3ab8c3c7d4648769b31`.

Dívida strict: 21 -> 20 (W4A8 kernel) -> 19 (benchmark) -> 18 (GPU test) ->
**17 violações** após Capture32 hook support.

Próxima ação estrutural: `tools/quantize_w4a8_sm89.py` (422 linhas), mantendo o mesmo
entrypoint/script e sem adicionar job/executável.

## Revisão obrigatória

- [ ] Todo steady cost justificado.
- [ ] Benchmark exclui waits/Map/console.
- [ ] Warm-up separado; p50/p95/p99.
- [ ] Tail regression bloqueia.
- [x] Nenhuma exclusão criada para escapar do cap.
- [x] Split W4A8 não introduz virtual/heap/lock/indireção.
- [x] Mais arquivos não significam mais executáveis, jobs ou builds completos.

## Validação rápida

- [ ] CPU/fake long run.
- [ ] Benchmark carriers.
- [x] Correctness separado.
- [x] Relatório LOC vs baseline.
- [x] Hot translation-unit split: benchmark before/after.

## Gate

- [ ] Performance contract cumprido.
- [x] Arquivos novos/tocados: zero >300.
- [ ] Legado >300 restante está marcado para retirement antes da RC.
- [x] Estrutura de tests/build não aumentou CI desnecessariamente.

## Próxima fase

Fase 21.
