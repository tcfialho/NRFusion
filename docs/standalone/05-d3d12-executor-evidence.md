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

## Subgate 02 — pending-submission / submission epoch

Head validado: `594eb3395b245f08b593a6ade335dcccf9c76541`.

Novo boundary portátil:

- `include/nrfusion/NrSubmissionGate.hpp` — 21 linhas;
- `src/NrSubmissionGate.cpp` — 23 linhas;
- `tests/submission_gate_tests.cpp` — 54 linhas.

Semântica:

- `MarkCreated(N)` marca a feature pending;
- `ReadyFor(epoch <= N)` retorna false;
- primeiro `ReadyFor(epoch > N)` libera a feature;
- depois de liberada, chamadas subsequentes ficam prontas até novo create/reset.

Integração D3D12:

- `EnsureFeatureForEpoch()` registra o epoch somente em build novo;
- `EvaluateForEpoch()` usa o gate e delega à única `Evaluate()`;
- `Evaluate()` direto rejeita enquanto o gate estiver pending, impedindo bypass;
- `HostServer64` permanece read-only e continua no caminho legado sem epoch.

Durante a sessão houve uma implementação concorrente na mesma branch. A atualização non-fast-forward foi
rejeitada pelo GitHub; o head concorrente foi inspecionado e preservado. O único invariant ausente nele
era o bloqueio do `Evaluate()` legado durante pending, aplicado no commit
`594eb3395b245f08b593a6ade335dcccf9c76541`.

Validação:

- Portable run `35648336714`: **PASS**, 7/7;
- `nrfusion_submission_gate_tests`: PASS, 0,01 s;
- Windows run `35648336610`: **PASS**, 20/20;
- Windows compilou loader/lifecycle/dispatch;
- workflow final: `NRFusion Windows validation passed`.

Auditoria:

- uma única ocorrência de call boundary `evaluate_(...)`;
- declarations/definitions de `EnsureFeatureForEpoch` e `EvaluateForEpoch`: 1:1;
- `HostServer64` não foi tocado;
- maior arquivo tocado no subgate: 123 linhas;
- nenhum comentário longo novo.

## Subgate 03a — deferred retirement

Head validado: `8f8be6dbf0d86f883fe0141a894bf09cb5158b4c`.

O executor maduro não libera feature/surface imediatamente em rebuild: ele estaciona objetos por
32 evaluates porque o trabalho do jogo pode continuar em voo por vários frames.

Foi adicionado `NrDeferredRetirementQueue`:

- capacidade fixa: 64;
- delay padrão: 32 calls;
- storage: `std::array`;
- heap/lock interno: nenhum;
- `Park()` transfere ownership somente quando existe slot;
- overflow retorna false e deixa o ponteiro original intacto;
- `Tick()` libera somente ao vencer o countdown;
- `DrainAfterIdle()` existe apenas para teardown com garantia externa de GPU idle.

Integração:

- `D3D12NrExecutor::EnsureFeature()` chama `Tick()` a cada tentativa;
- rebuild deixa de executar `release_(feature_)` imediatamente;
- feature anterior é estacionada como `NrRetiredObjectKind::Feature`;
- queue cheia falha fechado sem perder a feature ativa;
- `ReleaseRetired()` também conhece `Resource`, mas nenhum scratch resource foi conectado ainda;
- `HostServer64` permaneceu read-only.

Teste portátil `nrfusion_nr_retirement_queue_tests` cobre:

- 32 ticks antes do release;
- feature/resource kinds;
- callback ausente não descarta ownership;
- drain explícito;
- delay zero fail-closed;
- capacity completa e overflow preservando pointer;
- 100.000 park/tick cycles;
- zero allocations no trecho de stress.

Validação:

- Portable run `35664783931`: **PASS**, 8/8;
- retirement queue: PASS, 0,01 s;
- Windows run `35664783982`: **PASS**, 21/21;
- retirement queue Windows: PASS, 0,01 s;
- loader/lifecycle/dispatch recompilados;
- integrated Windows validation: PASS.

Source sizes:

- `NrDeferredRetirementQueue.hpp`: 44;
- `NrDeferredRetirementQueue.cpp`: 48;
- `nr_retirement_queue_tests.cpp`: 117;
- `D3D12NrExecutor.hpp`: 126;
- `D3D12NrExecutorLifecycle.cpp`: 73.

### Resource-state audit para o próximo subgate

O fixture maduro mostra estados de repouso determinísticos:

| Resource | Repouso | Estados temporários |
|---|---|---|
| output/passScratch | UAV | NPSR |
| colorCopy | UAV | NPSR |
| hdrCopy | UAV | NPSR, COPY_SOURCE |
| colorSmall | UAV | NPSR |
| outputNative | UAV | NPSR |
| activeColor | UAV | COPY_DEST, NPSR |
| guide clone | COPY_DEST | NPSR |

O próximo subgate deve extrair owner/state tracking desses surfaces antes de HDR/residual/multipass.
