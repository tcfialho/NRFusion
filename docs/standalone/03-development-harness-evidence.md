# Fase 03 — Evidência do Development Harness

## Auditoria inicial

| Superfície | Linhas antes | Decisão |
|---|---:|---|
| `tests/simulation.cpp` | 34 | preservar |
| `D3D12TestHarness.hpp` | 146 | evoluir |
| `D3D12TestHarness.cpp` | 1084 | dividir antes de features |
| `main.cpp` | 48 | evoluir CLI |
| `synthetic_dx12_test.cpp` | 417 | read-only |
| `synthetic_dx12_scale_gate_test.cpp` | 268 | read-only |
| `synthetic_dx11_bridge_test.cpp` | 177 | read-only |
| `synthetic_opengl_test.cpp` | 70 | read-only |
| `ipc_host_test.cpp` | 300 | read-only |
| `capture32_roundtrip_test.cpp` | 654 | read-only |

## Split mecânico

O monólito de 1084 linhas foi removido e dividido por responsabilidade. Antes de features,
comparação automática confirmou corpos equivalentes, HLSL idêntico e target/CTest preservados.

Após as revisões, os principais sources do harness permanecem abaixo do limite:

| Arquivo | Linhas |
|---|---:|
| `D3D12HarnessRun.cpp` | 243 |
| `D3D12HarnessPipeline.cpp` | 188 |
| `D3D12TestHarness.hpp` | 181 |
| `main.cpp` | 131 |
| `D3D12HarnessScenario.cpp` | 129 |
| `D3D12HarnessResources.cpp` | 115 |
| `D3D12HarnessBenchmark.cpp` | 107 |
| `D3D12HarnessProvider.cpp` | 80 |
| `D3D12HarnessShaders.cpp` | 66 |
| `D3D12HarnessScene.cpp` | 65 |
| `D3D12HarnessCorrectness.cpp` | 40 |
| `D3D12HarnessMetrics.cpp` | 34 |
| `D3D12HarnessExecution.cpp` | 26 |
| `D3D12HarnessDispatch.cpp` | 11 |

## Correctness

O modo padrão permanece o fluxo legacy. Cenários opcionais:

- `steady`;
- `resize`;
- `reset`;
- `missing-guides`;
- `provenance`;
- `on-off`;
- `failure`;
- `all`.

O resize recria recursos reais e restaura as dimensões originais.

O reset agora é one-shot no provider: um pedido de reset marca exatamente o próximo
`FrameContext.resetHistory`, sem confundir o sinal com `cameraCut`.

O cenário provenance agora verifica que `AcquireFrame()` preserva:

- `hostFrameToken`;
- `viewId`;
- `configurationGeneration`.

Depois ele mantém a regressão N vs N+1 em `sourceFrameId`.

## Fallback/recovery

A revisão encontrou um falso positivo no gate original: o CTest executava apenas 60 frames,
mas a janela artificial de fallback começava no frame 61. O teste aceitava qualquer
`Serialized` de startup como prova do fallback.

Correção:

- CTest legacy passou de 60 para 120 frames;
- o harness registra async antes do frame 61;
- exige `Serialized` dentro de 61..90;
- exige retorno a `AsyncCompute` após o frame 90;
- a validação final só publica sucesso do ciclo quando os três sinais ocorreram.

O relatório final foi extraído de `D3D12HarnessRun.cpp` para
`D3D12HarnessCorrectness.cpp`, mantendo o hot source em 243 linhas.

## CLI fail-closed

A revisão encontrou outro falso positivo: opções desconhecidas ou sem valor eram ignoradas.
Também era possível combinar `--benchmark --scenario all` e executar apenas benchmark.

A CLI agora:

- rejeita opção desconhecida;
- rejeita valor ausente;
- rejeita inteiros zero para frames/dimensões/iterações;
- rejeita `--benchmark` junto com `--correctness`;
- rejeita `--benchmark` junto com `--scenario`;
- usa `from_chars`, sem exceção para parsing normal.

Regressões CTest:

- `nrfusion_harness_3d_cli_unknown`;
- `nrfusion_harness_3d_cli_conflict`;
- `nrfusion_harness_3d_cli_zero_frames`.

Todos usam `WILL_FAIL` e terminam antes da inicialização D3D12.

## Benchmark

`--benchmark` mede somente:

`AcquireFrame → ReadyForCore → ResolveAuto`

O trecho medido não contém wait, `Map`, console, filesystem nem setup de recursos.
Há 256 warmups e buffer pré-alocado. O relatório contém iterations, not-ready,
unsupported, p50, p95 e p99.

## Histórico de correções da Fase 03

Primeira revisão/validação:

- short-circuit podia pular cenários posteriores e a restauração do resize;
- `Halton()` perdeu `return r;` durante o split;
- manifest flatten do OptiScaler omitia `FrameContract.hpp`.

Segunda revisão adversarial:

- CTest legacy não alcançava a janela de fallback;
- provider do harness descartava identidade adicional do FrameContract;
- CLI era fail-open;
- cenário reset escrevia manualmente o próprio resultado e não exercitava estado do provider.

Commits da segunda revisão:

- `6cda14f9b0418f90c2dddcd1b6455316b13d679c` — frame identity;
- `86df11f3bda93e4d7fc2c12965efd063213d8bbb` — fallback/recovery;
- `d9e3c9f1463942177b302222e91d3f6c481b0821` — CLI fail-closed;
- `4cda2b92f8bcd6b7b14c38fb4517411000624044` — reset one-shot;
- `110244459754fda9b7fadec728b72d5e5f210f76` — reject missing option values.

## Validação final da segunda revisão

Head validado: `110244459754fda9b7fadec728b72d5e5f210f76`.

Portable Core run `35624847763`: **PASS**.

Windows run `35624847807`: **PASS**.

CTest Windows:

- `nrfusion_harness_3d`: PASS, 120 frames, 7,20 s;
- `nrfusion_harness_3d_scenarios`: PASS, 0,06 s;
- `nrfusion_harness_3d_benchmark`: PASS, 0,06 s;
- três regressões CLI: PASS, 0,01 s cada;
- total: **18/18 PASS**.

O mesmo workflow terminou com `NRFusion Windows validation passed`, incluindo build integrado,
distribuição OptiScaler e validação NSIS.

## Gate Fase 03

Fechado após segunda revisão adversarial. Fase 04 não foi iniciada.
