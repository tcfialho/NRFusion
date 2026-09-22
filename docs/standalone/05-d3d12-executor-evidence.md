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


## Subgate 03b — scratch owner inicial

Histórico foi reancorado sem perda: o antigo head `db70d1632121a56a27aa2df630d0df644278a401`
e o novo head reancorado `97c5720a1687e3b4f243b2bddd1ce30357386519` possuem a mesma tree
`87c35a95f9380da24f90cad40b5787ef37b885be`. A branch ficou linear como master + dois WIP.

Implementação:
- `D3D12NrScratchResources` possui `output`, `colorCopy` e `hdrCopy`;
- estado inicial/repouso é UAV;
- resize usa retirement em vez de release imediato;
- transition exige o estado anterior conhecido;
- enum inválido agora falha fechado em vez de selecionar `hdrCopy`.

A revisão encontrou esse fallback de enum inválido no WIP e o corrigiu no commit
`a9d719285b5b1b35e30840c53654ebe859a226c3`.

Build rápido:
- `nrfusion_nr_scratch_resources_tests` deixou de linkar `nrfusion_core`;
- o target compila somente `D3D12NrScratchResources.cpp`, `NrDeferredRetirementQueue.cpp` e o teste;
- commit: `93ab8359029ffba7977e6600ad9d1ae163363962`.

Regressão WARP:
- cria os três surfaces sem depender de GPU física;
- verifica idempotência;
- exercita UAV -> NPSR -> UAV;
- rejeita state-before incorreto;
- resize estaciona 3 recursos;
- retire estaciona os 3 novos;
- drain libera os 6 recursos.
- commit: `a4dd528c3a95cc026b853c7f0d9384d941d70d1d`.

Source sizes deste lote:
- header scratch: < 100 linhas;
- implementação scratch: < 200 linhas;
- teste WARP: 121 linhas;
- CMake Windows: 157 linhas.

Validação deste head: revisão estrutural concluída; nenhum full build/CI foi disparado.
O próximo build deve ser somente o target focado de scratch + seu CTest.


## Subgate 03c — frame planning standalone

Extraído do executor maduro sem dependência de OptiScaler:

- working scale: clamp 0.25..2.0, NaN -> 1.0, tamanho arredondado com +0.5;
- pass limit: 3 normal, 30 destravado;
- proxy backend: exatamente 1 pass;
- subrects de color/depth/motion validados contra suas surfaces;
- crop/padding pre-SR explícito;
- motion-to-work scale calculado por eixo.

Arquivos:
- `include/nrfusion/D3D12NrFramePlan.hpp`: 52 linhas;
- `src/D3D12NrFramePlan.cpp`: 69 linhas;
- `tests/d3d12_nr_frame_plan_tests.cpp`: 78 linhas.

Validação isolada Linux:
`g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror`: PASS.

A auditoria deste ponto confirma que a Fase 05 continua aberta: o fixture ainda é o único owner
da semântica GPU de codec/encode/resolve, multipass real, HDR/exposure/residual e seams SR/RR.


## Revisão adversarial do seed após 03c

Encontrado um defeito de failure ordering em `EnsureFeature()`:

- a feature ativa era estacionada antes de validar `cmdList`/adquirir o device;
- uma falha de `GetDevice()` portanto podia destruir o active path sem sequer tentar create;
- `Evaluate()` também deixava ponteiros nulos e dimensão zero chegarem à boundary externa.

Correção:
- valida width/height antes de mutação;
- exige command list somente quando create/rebuild é realmente necessário;
- adquire e valida o device antes de estacionar a feature antiga;
- evaluate rejeita command list/resources nulos e dimensões zero;
- retired-kind inválido deixa de cair implicitamente no cast de Resource.

Nenhum full build foi disparado; este patch requer o próximo Windows fast junto do scratch/frame-plan.


### Build isolation do frame planner

O primeiro registro do teste usava `nrfusion_test()`, que linka `nrfusion_core` e faria um target
focado depender do core inteiro. Foi corrigido: `nrfusion_d3d12_nr_frame_plan_tests` agora compila
somente `D3D12NrFramePlan.cpp` + o teste, com warnings-as-errors no target.


Frame-plan review: os subrects validados de color/depth/motion agora são preservados no resultado do
plan, evitando que offsets já validados sejam perdidos/recalculados downstream. Regressão isolada:
PASS com `-Wall -Wextra -Wpedantic -Werror`.


Naming review: o bloco parcial foi renomeado de `D3D12NrExecutionSnapshot` para
`D3D12NrFramePlanConfig`; ele configura apenas planejamento de frame e não deve ser confundido
com o snapshot operacional completo ainda pendente no gate da Fase 05.


## Verificação de estabilização da sessão

Head verificado: `e45247bbdd597d7f755e9f7d443747b96f85f21a` antes do handoff documental.

- branch de integração: 13 commits à frente de `master`, 0 atrás;
- PRs abertos: 0;
- arquivos de código tocados neste lote permanecem abaixo de 300 linhas;
- frame planner: compile/test isolado C++20 com warnings-as-errors PASS;
- scratch WARP e hardening do executor ainda requerem Windows fast;
- nenhum full Windows build foi disparado.

A auditoria confirma que `scratch_` e o frame planner ainda não formam o frame path completo.
Existência do owner/planner não é contada como integração GPU.


## Subgate 03d-a — transient surface ownership

`D3D12NrScratchResources` foi expandido para:
- `passScratch` em work size;
- `colorSmall` em work size;
- `outputNative` em frame size;
- `activeColor` em frame size.

A API separa o trio obrigatório de optional surfaces para não realocar `output/colorCopy/hdrCopy`
quando apenas passes/crop/scale mudarem. Em troca, uma mudança real de frame/work geometry aposenta
todas as surfaces dependentes, igual ao lifecycle maduro.

A regressão WARP foi expandida para verificar tamanho real do resource, resize individual,
idempotência, transitions, retire seletivo e invalidação de optionals em geometry rebuild.

Windows fast continua pendente porque o conector desta sessão não expõe `workflow_dispatch`;
o workflow permaneceu manual-only e não foi adulterado para contornar essa limitação.


### 03d-a review correction + residual carriers

A revisão estática encontrou e corrigiu um erro antes de qualquer Windows run: o helper
`MakeSurface` estava fora da classe e nomeava o tipo privado `Surface`. Ele passou a ser método
privado estático da classe.

O mesmo owner agora inclui `residualEdited`, `residualHistory[0/1]` e `residualComposed`.
As carriers continuam criadas em UAV e o teste explicita a primeira transição para NPSR, preservando
o lifecycle do executor maduro em vez de esconder a transição na criação.


## Subgate 03d-b — guide clone owner

Commit de implementação: `3f306234b8eff82c0d9ae5764538fa2faab484fa`.

Novo `D3D12NrGuideClones`:
- depth/motion separados do owner UAV;
- clone copia shape/layout/sample metadata da source;
- formato tipado fornecido pelo caller;
- flags removidas como no helper maduro;
- estado inicial COPY_DEST;
- transition valida state-before;
- descriptor idêntico é idempotente;
- resize/format change usa deferred retirement;
- shutdown do executor libera clones após GPU idle.

Target `nrfusion_nr_guide_clones_tests` é isolado de `nrfusion_core` e usa WARP.
Revisão estática posterior adicionou `<cstddef>` diretamente para `std::size_t`.

Windows fast permanece pendente: o conector disponível não oferece `workflow_dispatch`.


## Auditoria de provenance do codec

O fixture local não contém:
- `shaders/dlssnr/precompile/dlssnr.hlsl`;
- `shaders/dlssnr/precompile/DlssNr_Shader.h`;
- `shaders/dlssnr/DlssNr_Dx12.h`.

O lock de upstream aponta para
`wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass@1b1dd650d35ea59ea2d1d0bf7937b71645159f75`.

Nesse commit foram verificados:
- `dlssnr.hlsl`: blob `4a6102820f736e9349ffed370259d094f2a7f4ae`;
- `DlssNr_Shader.h`: blob `23429d34833b5f4ad761f83446998d518217183d`;
- `DlssNr_Shader.cso`: blob `d6eaab373d6f07142af5c283c1acc4b49edba351`;
- `dlssnr_residual.hlsl`: blob `1aa829e15bd849be6b38e3f9a4265d0405f444be`.

O shader declara derivação/atribuição RenoDX e referencia
`Licenses/RenoDX_ATTRIBUTION.txt`, porém esse arquivo/pasta não existe no snapshot travado.
A busca por `RenoDX_ATTRIBUTION` no upstream retornou 4 resultado(s).

Conclusão: subgate 04 não deve vendorizar shader/CSO ainda. Primeiro precisa fechar attribution e
reprodução do CSO a partir do HLSL fixado.
