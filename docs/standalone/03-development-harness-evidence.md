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

## Validação estrutural

- todas as 17 definições de métodos esperadas aparecem exatamente uma vez;
- todos os arquivos adicionados ao harness aparecem exatamente uma vez no target CMake;
- `D3D12TestHarness.cpp` não existe nem é referenciado;
- todos os sources novos/tocados têm <=300 linhas;
- nenhum bloco de comentário novo/tocado ultrapassa 120 caracteres;
- todos os nomes de cenário estão ligados à CLI.

## Revisão do lote

A revisão adversarial do código novo encontrou um erro no runner de cenários: após a primeira falha,
o uso de short-circuit podia pular cenários posteriores e, no resize, podia pular a restauração do
tamanho original. O runner agora executa todas as validações selecionadas e restaura recursos
independentemente do resultado intermediário.

Comentários que apenas repetiam operações foram removidos. O maior source do harness ficou com
248 linhas sem compactar statements.

## Limite atual

Este ambiente não possui Windows SDK/Mingw para compilar D3D12. Os workflows possuem
`workflow_dispatch`, mas o connector GitHub disponível nesta sessão não expõe uma ação para
dispará-lo. Nenhum CI foi disparado artificialmente.

A Fase 03 só fecha o gate final depois de uma execução Windows estabilizada do mesmo lote.
