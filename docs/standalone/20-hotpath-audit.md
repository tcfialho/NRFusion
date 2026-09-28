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

## Subgate 20c — tooling e probe boundaries

`70d360c` dividiu o quantizer W4A8:
- entrypoint `quantize_w4a8_sm89.py`: 247 linhas;
- core matemático: 195 linhas;
- mesmo CLI/entrypoint; assinatura determinística before/after
  `842bc26ef094b931ae38c81d6ed778608ed5125c23f351e6067a4af4325eb459`;
- Portable `36372671947`: PASS.

`e1e239b` dividiu o extractor de pesos em entrypoint 153, parser PE 140 e
parser/modelo 146 linhas. Um mapa WEIGHTS_HT sintético mínimo preservou assinatura
`4865693ee64fe5e1836d7d664bc26448e6dbb4dfd2e70bb48c39ef884c59b5fa`.
Portable `36372871149`: PASS.

`6e6a44b` moveu apenas discovery/carving/cuobjdump para helper de 94 linhas;
`decode_swin_abi.py` ficou em 297. CLI, `cuobjdump` real e classificação sintética
continuam válidos. Portable `36373040235`: PASS.

`88a3bf5` separou a API pública do probe ABI no mesmo translation unit:
`NrKernelAbi.cpp` 206 linhas + API 115; reconstrução byte-a-byte.
Portable `36373144165`, Focused `36373144156`, Windows `36373144224`: PASS.
Checkpoint `10949588828`, sha256
`20ce914c65feae3ac6c26dbb389822490c3beb9c07a905c93276ba490c04c8bc`.

Dívida strict ao final do lote: **13 violações**, contra 21 no início da Phase 20.

`CompatibilityDatabase.cpp` não foi dividido: assim como `ProfileStore.cpp`, ele está na
lista fechada de sources materializada por `tools/apply_to_optiscaler.py`. Adicionar fragment
novo sem uma boundary limpa no patcher repetiria a falha de closure já observada; fica bloqueado
até o patcher deixar de ser monolítico/fechado.

## Subgate 20d — test harness structural debt

`2477f6d` separou somente a validação numérica de residual de
`tests/synthetic_dx12_test.cpp`: principal 276 linhas + fragment 142, mesmo translation unit
e executável, com reconstrução textual exata.
Focused `36375068164`, Portable `36375068167`, Windows `36375068166`: PASS.
Checkpoint `10949829126`, sha256
`6bd8d31d819df2ddd9b1df72de28f3461d1d7ae2217afb8c73e170268f74bbc2`.

`d026c24` separou o bloco de criação/upload/dispatch/readback de
`tests/residual_gpu_test.cpp`: principal 254 linhas + fragment 242, mesmo translation unit
e executável, com reconstrução textual exata.
Focused `36376310469`, Portable `36376310422`, Windows `36376310451`: PASS.
Checkpoint `10951195647`, sha256
`65742fee6dff9bfe96eb6b2303662c6be7c5d9e232e78e4ae008c279002b19e6`.

`1115441` separou os cenários de `InstallerState` de
`tests/game_probe_tests.cpp`: principal 273 linhas + fragment 279, mesmo test executable,
com reconstrução textual exata.
Focused `36376791161`, Portable `36376791143`, Windows `36376791288`: PASS.
Checkpoint `10951037881`, sha256
`eb607ef5b6616d56724de10b49f00daf77b044234a9285e57fb3ea7495130b49`.

Dívida strict: **13 -> 10 violações** neste lote; arquivos novos/tocados continuam <=300.

## Subgate 20e — installer state e Requiem testbed

`76c064d` separou `src/InstallerState.cpp` por responsabilidade:
- principal 205 linhas;
- install/snapshot 170;
- restore/transaction 238;
- mesmo translation unit, API e target;
- reconstrução textual exata.
Focused `36377571978`, Portable `36377571938`, Windows `36377571748`: PASS.
Checkpoint `10951711911`, sha256
`d572b805b12b7d46bac93ff89e8e2da4e7d3e5ce2a133d920a9376875f2c945f`.

`98651fb` separou os estágios do testbed Requiem:
- `main.cpp` 146 linhas;
- args 51, setup 267, render loop 181;
- mesmo executável/translation unit;
- reconstrução textual exata;
- `--fixed-scene`, `--deterministic-motion` e `D3D12 Testbed` continuam no main para
  preservar os contratos grep de `tests/test_patcher.sh`.
Portable `36378070733`: PASS.
Como `tools/requiem_game/**` não entra nos path filters dos outros workflows, foram
disparados gates direcionados no mesmo HEAD:
Focused `36378152527` PASS e Windows fast `36378154674` PASS.
Checkpoint `10952220132`, sha256
`f2a3d45117db09138d7ba99134e6596be77b5f8aa082d004ef084815cfe4e2c9`.

Dívida strict: **10 -> 8 violações** neste lote; todos os arquivos tocados permanecem <=300.

## Subgate 20f — distribution packaging

`c354c6a` separou `tools/build_dist.ps1` por estágio:
- entrypoint 262 linhas;
- W4A8 assets 86;
- carrier/testbed 50;
- CLI e fluxo continuam únicos via dot-source no mesmo escopo;
- os tokens exigidos por `test_dist_contract.sh` permaneceram no entrypoint.
Parser PowerShell dos três arquivos: PASS.
`tests/test_dist_contract.sh`: PASS em Git Bash.
Portable `36378699825`: PASS; Focused `36378773440`: PASS.
Checkpoint `10952137046`, sha256
`211bc9184a40f583a6306b9e6ea73686ecf80834dc30aceed7add538a842d274`.
Windows full `36378775434` permanece em execução no freeze.
Dívida strict: **8 -> 7 violações**.

Próxima ação estrutural: `installer/NRFusion.nsi` (465 linhas). Dry-run já separa
`Install` e `Uninstall` em includes de 153/38 linhas, deixa o root em 277 e reconstrói
o source atual exatamente. Os testes de distribuição/UX devem validar root + includes como
uma única closure para preservar as mesmas asserções sem duplicar strings no root.

## Revisão obrigatória

- [ ] Todo steady cost justificado.
- [x] Benchmark exclui waits/Map/console.
- [x] Warm-up separado; p50/p95/p99.
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
