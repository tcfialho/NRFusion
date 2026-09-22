# Fase 06 — NrSession — uma transação por frame

## Status

**Em andamento.** Subgates 06a–06e implementados e revisados; gates de execução portátil ainda pendentes.

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
- [x] `PerformanceController.cpp` não foi expandido nesta fase; cost learning permanece separado em `NrCostModel`.
- [x] `ProfileStore.cpp` não foi tocado; persistence ficou fora do `NrSession`.

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


## Subgate 06e — hardening final da sessão

### Config identity e reconfigure transacional

- a mesma `RuntimeConfig.generation` só é idempotente quando `RuntimeConfig` **e**
  `PerformanceConfig` são iguais;
- config/performance antigos são preservados quando a nova configuração é inválida;
- exhaustion de runtime generation falha fechado antes de mutar a sessão;
- work/timing antigos só são resetados depois de uma reconfiguração válida;
- `Reset() noexcept` não reconstrói `PerformanceConfig`, evitando allocation/throw dentro de
  `noexcept`.

### Ownership cleanup

O `FusionRuntime` não possui mais os estados legados que duplicavam ownership dentro de
`NrSession`:
- `WorkLedger` / `TimingWorkMapper`;
- `TelemetryTracker`;
- `PipelinedExecutorState`.

Os equivalentes necessários pertencem explicitamente ao `NrSession` ou ao adapter legado.
Isso remove também allocations de construção que não tinham uso no standalone.

### Precision timing

`RetireTimedInterval` agora alimenta o `PrecisionAutotuner` central usando:
- precision exata do `WorkTicket.precisionTag`;
- runtime generation exata do ticket;
- GPU timing validado.

O mesmo completion continua alimentando o cost model da working scale. A regressão percorre baseline
e candidate e exige seleção de HybridNvfp4 quando a candidate medida é mais rápida.

### Differential e slice portátil

- o diferencial compara 180 decisões adaptativas consecutivas entre `NrSession` e um
  `FusionRuntime` independente;
- existe regressão para reconfigure inválido, resize/generation stale, timing inválido, reset e
  precision qualification;
- `NRFUSION_FOCUSED_NR_SESSION_VALIDATION=ON` cria um slice portátil isolado;
- o modo focado não é construído por padrão, portanto não duplica production units em build normal;
- os targets focados são `nrfusion_nr_session_tests` e
  `nrfusion_nr_session_stress_tests`.

### Stress de allocations

O stress contém:
- 512 frames de warmup;
- 1.000.000 transações completas;
- `operator new/new[]` instrumentado somente na janela medida;
- gate esperado: 0 allocations.

A auditoria removeu uma allocation steady real:
`ResolveAuto -> CheaperPrecision -> SupportedPrecisions -> std::vector`.
`CheaperPrecision` agora é decisão direta e semanticamente equivalente.

## Estado dos gates

Implementação/revisão estrutural:
- [x] call graph standalone sem `OptiScalerAdapter`;
- [x] nenhum mutex no `NrSession`;
- [x] work/timing com storage fixo;
- [x] ownership único de work/timing/telemetry/pipeline state no standalone;
- [x] decisões diferenciais versionadas;
- [x] timing/reset/overload/config-change regressions versionadas;
- [x] todos os arquivos first-party tocados <=300 linhas.

Execução ainda pendente neste ambiente:
- [ ] executar `nrfusion_nr_session_tests`;
- [ ] executar `nrfusion_nr_session_stress_tests`;
- [ ] confirmar 0 allocations no milhão de frames;
- [ ] promover os testes diferenciais/versionados de "implementados" para "PASS".

O container possui compiladores e CMake, mas DNS para github.com continua indisponível e o conector
GitHub não expõe workflow dispatch. Nenhum Windows gate foi usado.
