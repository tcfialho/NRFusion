# Fase 06 — NrSession — uma transação por frame

## Status

**Em andamento.** Subgates 06a–06d implementados; gates de execução portátil ainda pendentes.

## Objetivo

Consolidar policy/runtime/telemetry sem criar novo God Object e reduzir dívida existente do core.

## Dependências

Fase 05.

## Fora de escopo

- Mudar policy por conveniência
- Otimizar GPU

## Implementação

- [x] Definir FramePacket/FrameResult mínimos.
- [x] Resolver scale/precision/placement/scheduler em uma transação.
- [x] Integrar work identity/timing retirement.
- [x] Remover OptiScalerAdapter e getters repetidos do call graph standalone.
- [x] Separar RuntimeConfig de state mutável.
- [x] Preservar stale timing/generation/reconfigure quarantine.
- [x] `NrSession` orquestra; helpers permanecem focados.
- [x] Antes de mudanças substanciais, decompor `FusionRuntime.hpp` (>300) em contratos/facades pequenos.
- [ ] Se `PerformanceController.cpp` for evoluído, separar estimativa/cost learning da state machine de escala.
- [ ] `ProfileStore.cpp` só é tocado após separar codec/validation de persistence, se ainda fizer parte do standalone.

## Revisão obrigatória

- [x] Old vs new call graph.
- [x] Standalone NrSession não possui lock; locks do adapter ficaram confinados ao legado.
- [x] Nenhum state standalone tem dois owners.
- [x] Header contém contrato, não implementação escondida.
- [x] Split não duplica policy.

## Validação rápida

- [ ] Fake executor: teste de 1.000.000 frames implementado; execução portátil pendente.
- [ ] Differential decisions.
- [ ] Timing/reset/overload/config changes.
- [x] LOC checker estrutural: todos os arquivos first-party tocados <=300.

## Gate

- [ ] 0 heap allocations steady.
- [x] Menos locks/calls: standalone remove singleton/mutex/getters do adapter.
- [ ] Mesmas decisões equivalentes.
- [x] Core tocado <=300 linhas por arquivo.

## Próxima fase

Fase 07.


## Subgate 06a — contrato e configuração

- `NrSessionFramePacket` reúne game/frame/telemetry/capabilities e parâmetros de policy sem heap.
- `NrSessionFrameResult` devolve disposition + `AutoDecision` + gerações.
- `RuntimeConfig` permanece configuração; `NrSessionState` concentra state mutável.
- frame cuja `configurationGeneration` não coincide é rejeitado antes de `ResolveAuto`.
- reconfigure com generation antiga é rejeitado; generation nova abre novo runtime epoch.
- `BeginConfigurationEpoch` invalida state adaptativo antigo e avança a generation de precision.
- decisão válida é diferencialmente comparada com um `FusionRuntime` independente no teste portátil.
- `FusionRuntime.hpp` deixa de possuir helpers/identity de Auto; eles passam ao contrato focado
  `AutoDecision.hpp`, mantendo funções inline e sem custo de chamada adicional.
- o overlap calibrado, que é timing/diagnóstico e não policy central, foi movido para
  `FusionRuntimeTiming.cpp`; o header volta a ficar abaixo do limite estrutural.

Próximo subgate: 06b — work identity/timing retirement com storage fixo e fake executor, removendo
o caminho por-frame de `OptiScalerAdapter` sem introduzir locks ou allocations steady.


## Subgate 06b — work/timing fixos

`NrSessionWorkTracker` substitui o caminho dinâmico dentro da nova sessão:
- 64 work tickets fixos;
- ticket exato, begin/submit/complete/abandon;
- IDs não são reutilizados;
- reconfigure troca session namespace e limpa outstanding work.

`NrSessionTimingQueue` usa ring fixo de 16 entradas:
- overflow desloca o work mais antigo e o abandona;
- invalid timing ocupa sua posição para não completar o próximo work por engano;
- reconfigure limpa o ring;
- timing só treina custo quando ticket/session/config generation ainda pertencem à configuração atual.

Nenhum `unordered_map`, `vector`, lock ou heap foi introduzido na nova work/timing boundary.


### 06b adversarial correction

O primeiro draft capturava `runtimeGeneration` antes de `ResolveAuto`. Isso era incorreto porque
`ResolveAuto` pode abrir nova generation ao mudar estrutura/precision. Corrigido:
- `FrameResult.runtimeGeneration` é capturada depois da decisão;
- `WorkTicket.configurationGeneration` carrega a runtime execution generation;
- submit/map/retire validam contra a generation atual;
- timing de work anterior a resize/precision/scheduler epoch é completado/descartado sem treinar custo;
- exhaustion do namespace de session deixa `Begin` fail-closed.


- exhaustion de `session_` é terminal: namespace 0 permanece inválido e nunca recicla para 1.


## Subgate 06c — steady-state allocation stress

A auditoria do hot path encontrou uma allocation evitável: `CheaperPrecision()` chamava
`SupportedPrecisions()`, que materializa um `std::vector`. Como `ResolveAuto()` consulta
`CheaperPrecision()` no steady path, isso podia alocar por frame.

Correção:
- `CheaperPrecision()` agora resolve diretamente a única transição válida FP8 -> HybridNvfp4;
- `SupportedPrecisions()` permanece inalterado para enumeração/menu fora do hot path;
- sem mudança de policy ou resultado.

Regressão portátil adicionada:
- 512 frames de warmup;
- 1.000.000 frames medidos;
- fake executor percorre Resolve -> Begin -> Submit -> MapTiming -> Retire;
- `operator new/new[]` do executável contam allocations somente na janela medida;
- gate: exatamente 0 heap allocations no milhão de frames.


### Estado de validação 06c

O teste de stress está versionado, mas **não foi executado nesta sessão**:
- o ambiente local possui C++ compiler/CMake, porém não possui checkout do repositório;
- o conector GitHub disponível não expõe workflow dispatch;
- nenhum Windows gate foi usado.

Portanto o gate de 0 allocations permanece aberto até a execução efetiva do target
`nrfusion_nr_session_stress_tests`.


## Subgate 06d — retirada do OptiScalerAdapter do standalone

Auditoria do call graph confirmou que `OptiScalerAdapter` não é consumido por nenhum source standalone:
os usos restantes são o próprio adapter, o patcher legado e `controller_tests.cpp`.

Mudança:
- `src/OptiScalerAdapter.cpp` saiu de `nrfusion_core`;
- o source é compilado somente em `nrfusion_tests`, que preserva as regressões legadas;
- header/source continuam versionados para o patcher OptiScaler legado;
- nenhum source de `NrSession`, RuntimeShell, executor ou provider referencia o adapter.

Call graph antigo:
`patched OptiScaler -> OptiScalerAdapter singleton/mutex -> FusionRuntime -> policies/work/timing`.

Call graph standalone:
`carrier/provider -> FrameContract/NrSessionFramePacket -> NrSession -> FusionRuntime policy + fixed work/timing`.

Consequências estruturais:
- singleton do adapter fora do core standalone;
- mutex do adapter fora do frame path standalone;
- `LastDecision`, `LastAutoDecision` e `FusionRuntimeEngine` não participam do novo fluxo;
- policy continua única em `FusionRuntime`; `NrSession` apenas orquestra e possui state de transação.


## Checkpoint 06d

Verificação estrutural no head `b98309b9e1c81a58293c3fae918ca99eb9859db3`:
- `nrfusion_core` não contém `OptiScalerAdapter.cpp`;
- `nrfusion_tests` compila o adapter explicitamente para manter regressões legadas;
- `NrSession` não contém mutex/scoped_lock;
- `NrSessionWorkState` não contém vector/deque/unordered_map;
- arquivos tocados: core CMake 109, tests CMake 37, NrSession.hpp 43,
  NrSession.cpp 123, work header 71, work source 121;
- branch +54/-0 contra master; PR aberto 0.

Gates ainda abertos por execução, não por implementação:
- differential decision test;
- timing/reset/overload/config-change tests;
- fake executor de 1.000.000 frames;
- prova runtime de 0 allocations steady.


## Subgate 06e — config identity + portable validation slice

A configuração da sessão agora considera `RuntimeConfig` **e** `PerformanceConfig` na mesma generation.
Uma tentativa de mudar performance mantendo a generation antiga falha fechada em vez de ser ignorada.

O CMake ganhou `nrfusion_nr_session_portable`, composto apenas pelas production units portáteis
necessárias ao `FusionRuntime/NrSession`. Os dois testes de sessão deixam de linkar `nrfusion_core`,
portanto não puxam CUDA, hosts, carriers Windows, codecs D3D12 ou OptiScalerAdapter.

O teste diferencial foi ampliado de um frame para 180 decisões adaptativas consecutivas, comparando
`NrSession` e um `FusionRuntime` independente inclusive durante mudança de pressão GPU.


### 06e adversarial correction — transactional Configure

`NrSession::Configure()` agora preflighta exhaustion da runtime generation e valida
`PerformanceConfig` antes de invalidar work/timing da configuração corrente.

Se `PerformanceController` rejeita o novo config (por exemplo, nenhum scale step finito):
- `Configure` retorna false;
- RuntimeConfig anterior permanece;
- work/timing anteriores não são resetados;
- a sessão anterior continua resolvendo frames válidos.

Isso remove um partial-reconfigure path sem adicionar custo ao steady frame path.


### 06e ownership cleanup

`FusionRuntime` ainda carregava `WorkLedger` e `TimingWorkMapper` legados mesmo depois de
`NrSession` assumir work/timing fixos. O adapter legado já possui trackers próprios e nenhum callsite
usa `FusionRuntime::Works()/TimingMap()`.

Removidos do runtime central:
- os dois getters;
- os dois members;
- includes correspondentes.

Efeito: um único owner de work/timing no standalone e remoção da allocation de construção do
`TimingWorkMapper{16}` que era inútil em cada `NrSession`.


### 06e runtime-state cleanup

A busca de consumidores confirmou que `FusionRuntime::Telemetry()/Pipeline()` não eram usados.
O adapter legado já possui `TelemetryTracker` e `PipelinedExecutorState` próprios, enquanto
`NrSession` recebe `TelemetrySample` explícito e não usa pipeline state interno.

Removidos do `FusionRuntime`:
- `TelemetryTracker telemetry_` + getters;
- `PipelinedExecutorState pipeline_{2}` + getters.

Isso remove mais um vector allocation da construção da sessão e reduz state duplicado sem alterar
`ResolveAuto`, scheduler, precision, cost learning ou contratos de frame.
