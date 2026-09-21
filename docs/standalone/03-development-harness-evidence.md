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

O monólito de 1084 linhas foi removido e substituído por arquivos por responsabilidade:

| Arquivo | Linhas |
|---|---:|
| `D3D12HarnessLifecycle.cpp` | 161 |
| `D3D12HarnessResources.cpp` | 115 |
| `D3D12HarnessShaders.cpp` | 66 |
| `D3D12HarnessPipeline.cpp` | 188 |
| `D3D12HarnessScene.cpp` | 65 |
| `D3D12HarnessExecution.cpp` | 26 |
| `D3D12HarnessProvider.cpp` | 75 |
| `D3D12HarnessDispatch.cpp` | 11 |
| `D3D12HarnessRun.cpp` | 248 |
| `D3D12HarnessScenario.cpp` | 118 |
| `D3D12HarnessBenchmark.cpp` | 107 |
| `D3D12HarnessMetrics.cpp` | 34 |

Antes de qualquer feature, comparação automática confirmou:

- 15 funções originais com corpos equivalentes;
- HLSL idêntico;
- CMake idêntico fora da lista de fontes do mesmo target;
- target e CTest existentes preservados.

Após a separação de modos, o corpo do correctness legado permaneceu código-equivalente
ao `Run()` original, ignorando apenas comentários e whitespace.

## Correctness

O modo padrão permanece o fluxo legado.

Cenários focais opcionais por `--scenario`:

- `steady`: frame normal e suporte D3D12;
- `resize`: recria recursos reais em metade da resolução e restaura o tamanho original;
- `reset`: mantém camera cut e reset-history como sinais separados;
- `missing-guides`: remove guias opcionais e valida fallback do contrato;
- `provenance`: aceita evidência completa do frame correto e rejeita recurso de frame errado;
- `on-off`: valida lifecycle Disabled → Running → Disabled;
- `failure`: remove o color obrigatório e exige rejeição;
- `all`: executa todos os cenários focais.

Não foi criado outro executável ou mini-game.

## Benchmark

`--benchmark` mede somente:

`AcquireFrame → ReadyForCore → ResolveAuto`

O trecho cronometrado não contém:

- wait/fence wait;
- `Map`;
- console;
- filesystem;
- setup de recursos.

Há 256 iterações de warmup antes da coleta.

A coleta usa buffer pré-alocado e reporta:

- iterations;
- not-ready count;
- unsupported count;
- p50;
- p95;
- p99.

`--benchmark-report <path>` controla o baseline. O padrão é
`harness_3d_benchmark.txt`. Se o arquivo já existir, a saída mostra before/after e,
após a medição, atualiza o baseline. Leitura/escrita ocorrem fora do trecho medido.

## Revisão do lote

A revisão adversarial do código novo encontrou um erro no runner de cenários: após a primeira falha,
o uso de short-circuit podia pular cenários posteriores e, no resize, podia pular a restauração do
tamanho original. O runner agora executa todas as validações selecionadas e restaura recursos
independentemente do resultado intermediário.

A primeira validação Windows encontrou uma regressão mecânica do split: `Halton()` perdeu
`return r;`. O MSVC emitiu C4716 e o build falhou. O retorno foi restaurado no commit
`8926c5afeccdd86d79282f1a095e9fa5759522cc`.

A primeira validação Portable compilou o core e passou 5/5 testes, mas o fixture do patcher detectou
closure flatten incompleto: `Types.hpp` e `FrameContractProvider.hpp` incluíam
`FrameContract.hpp`, que não estava no manifest do host. O manifest passou a copiar
`FrameContract.hpp` no commit `806262a355889f37857da0dd077413035e978692`.

Comentários que apenas repetiam operações foram removidos. O maior source do harness ficou com
248 linhas sem compactar statements.

## Validação final

PR draft de validação: #4.

Head de código validado: `9ddf010dd16ff5691a7059ba45788ba2bc0f8338`.

Portable Core run `35601662949`:

- build: PASS;
- testes portáveis: PASS;
- patcher fixture/closure: PASS.

Windows run `35601662951`:

- MSVC build: PASS;
- `nrfusion_harness_3d`: PASS, 5,29 s;
- `nrfusion_harness_3d_scenarios`: PASS, 0,06 s;
- `nrfusion_harness_3d_benchmark`: PASS, 0,06 s;
- CTest total: 15/15 PASS;
- distribuição OptiScaler integrada: PASS;
- NSIS/public developer dist: PASS;
- conclusão do workflow: PASS.

O log CTest suprime stdout de testes que passam, então os valores numéricos de p50/p95/p99
não aparecem no log do workflow. O benchmark executou as 10.000 iterações configuradas e retornou
sucesso; os percentis continuam disponíveis no output direto do runner e no arquivo de baseline.

## Gate Fase 03

Fechado. Fase 04 não foi iniciada.
