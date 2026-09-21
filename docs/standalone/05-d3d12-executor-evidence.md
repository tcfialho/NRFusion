# Fase 05 — Evidência parcial do executor D3D12

## Subgate 01 — canonicalização do seed standalone

Base: `dcb5ea54c9d36bc36a9009fd4ffebaa3c26ef1ca`.

O antigo `HostDlssNr` foi canonicalizado como `D3D12NrExecutor` sem alterar o callsite do
`HostServer64`.

Arquivos resultantes:

| Arquivo | Linhas |
|---|---:|
| `include/nrfusion/D3D12NrExecutor.hpp` | 110 |
| `include/nrfusion/HostDlssNr.hpp` | 9 |
| `src/D3D12NrExecutorLoader.cpp` | 153 |
| `src/D3D12NrExecutorLifecycle.cpp` | 46 |
| `src/D3D12NrExecutorDispatch.cpp` | 25 |
| `cmake/NRFusionCore.cmake` | 89 |

`HostDlssNr.hpp` contém somente o alias:

`using HostDlssNr = D3D12NrExecutor;`

Assim `HostServer64.cpp` e `HostServer64.hpp` permaneceram read-only.

## Prova mecânica

Antes de qualquer feature nova, os corpos foram comparados ignorando comentários/whitespace:

- `Load()`: equivalente;
- `DiscoverFloatSlot()`: equivalente;
- `Init()`: equivalente;
- `EnsureFeature()`: equivalente;
- `Evaluate()`: equivalente;
- `Shutdown()`: equivalente.

A lista CMake removeu `src/HostDlssNr.cpp` e adicionou exatamente loader/lifecycle/dispatch.

Nenhum comentário novo/tocado excede o limite do projeto.

## Regressão encontrada pelo MSVC

Primeiro head validado: `5ccf0a30b8a783065218c1792e3418e56a30a6b2`.

Portable run `35645350500`: **PASS**.

Windows run `35645350656`: **FAIL**.

Causa:

`D3D12NrExecutorDispatch.cpp` usava `kNgxSuccess`, mas a constante continuava no anonymous
namespace do loader após o split. O monólito antigo compartilhava essa constante no mesmo TU.

Fix:

- mover `kNgxSuccess = 1` para constante privada de `D3D12NrExecutor`;
- remover a definição local do loader;
- loader e dispatch passam a usar uma única definição.

Commit do fix: `cc932018692e2f1fc766a75985940bf0acb5a836`.

## Validação do fix

Portable run `35645666859`: **PASS**.

Windows run `35645666861`: **PASS**.

O log Windows confirma compilação de:

- `HostServer64.cpp`;
- `D3D12NrExecutorLoader.cpp`;
- `D3D12NrExecutorLifecycle.cpp`;
- `D3D12NrExecutorDispatch.cpp`.

CTest Windows: **19/19 PASS**.

Workflow integrado: **NRFusion Windows validation passed**.

## Próximo invariant maduro

O fixture de referência usa:

- `featurePendingSubmission`;
- `featureCreateEpoch`;
- `passPendingSubmission[]`;
- `passCreateEpoch[]`.

Create marca a feature como pending e registra o submission epoch. Evaluate no mesmo epoch retorna
sem executar o modelo; somente uma mudança de epoch torna a feature utilizável.

Esse será o próximo boundary. Resources/state/HDR/residual não serão portados antes dele.
