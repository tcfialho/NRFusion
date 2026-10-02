# Fase 06 — NrSession — uma transação por frame

## Status

**CONCLUÍDA.** Subgates 06a–06g implementados, revisados e validados no slice portátil.

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

- [x] Fake executor: 1.000.000 frames executados no slice portátil.
- [x] Differential decisions.
- [x] Timing/reset/overload/config changes.
- [x] LOC checker estrutural: todos os arquivos first-party tocados <=300.

## Gate

- [x] 0 heap allocations steady.
- [x] Menos locks/calls: standalone remove singleton/mutex/getters do adapter.
- [x] Mesmas decisões equivalentes.
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

O gate inicialmente ficou pendente por ausência de checkout local. Ele foi posteriormente executado
pelo workflow portátil focado introduzido em 06g; o estado final da fase é PASS.


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

Execução portátil concluída:
- [x] `nrfusion_nr_session_tests`;
- [x] `nrfusion_nr_session_stress_tests`;
- [x] 0 allocations no milhão de frames;
- [x] regressões diferenciais/timing/reset/overload/config-change em PASS.

Nenhum Windows gate foi usado.


### 06f — allocation hardening beyond steady plateau

A revisão do milhão de frames encontrou duas allocations de transição que o primeiro stress não
exercitava:
- `PrecisionAutotuner::Percentile` copiava vectors ao concluir a qualificação;
- `NrCostModel` podia crescer `points_` ao encontrar uma nova escala.

Correções sem alteração de algoritmo:
- o autotuner agora reutiliza um scratch vector reservado no reset/configure cold path;
- o cost model reserva no construtor do controller capacidade para todos os scale steps configurados;
- `Reset()` preserva essas capacidades.

O stress medido agora reconfigura antes da janela para:
- habilitar HybridNvfp4;
- usar target de 240 FPS;
- remover sustain/cooldown para forçar precision qualification e mudanças de escala dentro do
  intervalo de 1.000.000 frames.

Assim o contador de allocations cobre também candidate qualification e novos rungs, não apenas um
plateau FP8/scale fixa.


### 06f structural correction

A primeira versão reservava o cost model com uma chamada adicionada em `PerformanceController.cpp`,
arquivo legado >300. Isso violava a regra no-growth.

Correção:
- `PerformanceController.cpp` foi devolvido ao conteúdo anterior;
- `NrCostModel` recebe a capacidade no initializer de `PerformanceController.hpp`, que permanece
  abaixo de 300 linhas;
- a capacidade usa o tamanho original de `config_.scaleSteps`, que é >= ao conjunto normalizado,
  portanto cobre todos os rungs que o controller pode selecionar sem hipótese de tamanho fixo.


### 06f execution-generation separation

A revisão final detectou que a mesma generation estava sendo usada para duas responsabilidades:
- configuração interna do `PrecisionAutotuner`;
- quarantine de work da execução real.

Isso falhava especificamente em FP8 -> HybridNvfp4: o qualifier mudava a precision, mas o work epoch
não necessariamente mudava, permitindo timing de FP8 antigo contaminar o cost model da execução Hybrid.

Correção:
- `autoExecutionGeneration_` agora identifica shape/scheduler/scale/precision/support da execução;
- `autoPrecisionGeneration_` continua exclusivamente como generation interna do qualifier;
- mudança de precision avança execution generation sem resetar o qualifier já treinado;
- mudança estrutural/scale/scheduler avança as duas quando a precision config muda;
- supported -> unsupported também invalida imediatamente work antigo;
- `ObservePrecisionCost` primeiro valida execution generation e só então alimenta o qualifier com
  sua própria generation interna.

A regressão mantém um ticket FP8 pendente durante a qualificação e exige que ele seja rejeitado após
a primeira decisão Hybrid, provando a quarantine entre executions.


### 06f lifecycle split

A separação de execution/precision generations elevou `FusionRuntime.hpp` acima de 300 linhas.
Os métodos frios de lifecycle (`CanBeginConfigurationEpoch` e `BeginConfigurationEpoch`) foram
movidos para `FusionRuntimeLifecycle.cpp`.

O hot path permanece inline onde já estava; nenhum virtual/PImpl/lock/heap/indirect dispatch foi
adicionado para satisfazer o limite estrutural.


### 06f allocation-counter coverage

O stress agora intercepta também aligned `new/new[]` e respectivas formas de delete.
O caso `operator new(0)` também segue o contrato de allocation e não produz falso `bad_alloc`.
Assim o gate não ignora allocations apenas por serem over-aligned.


### 06f stress signal consistency

O fake executor não usa mais um timing constante desconectado da telemetry.
Cada completion agora reporta 4 ms em FP8 e 3 ms em HybridNvfp4; o mesmo valor vira
`packet.telemetry.nrGpuMs` do frame seguinte.

Isso preserva a semântica "telemetry descreve a execução anterior", permite qualificação real da
candidate Hybrid e mantém controller, cost model e precision tuner observando o mesmo workload.


## Checkpoint 06f

Head estrutural revisado: `4b19b4273fd0da1b798b3538c16df80c33bad8aa`.

Verificação final:
- branch +69/-0 contra `master`; PR aberto 0;
- `FusionRuntime.hpp` 296 linhas;
- `FusionRuntimeLifecycle.cpp` 22;
- `PrecisionAutotuner.cpp` 186;
- `NrCostModel.cpp` 167;
- `NrSession.cpp` 138;
- `nr_session_tests.cpp` 203;
- `nr_session_stress_tests.cpp` 172;
- `PerformanceController.cpp` continua 514 linhas, porém seu SHA é idêntico ao checkpoint anterior:
  nenhuma modificação/no-growth no arquivo legado;
- `NrSession` sem mutex;
- execution generation separada da precision qualifier generation;
- scratch de percentile reservado;
- cost-model capacity reservada antes do steady path;
- normal/aligned allocations instrumentadas;
- stress força Hybrid qualification e scale transitions.

A tentativa de reconstrução local por payload foi substituída pelo workflow portátil focado descrito
em 06g; os dois targets foram executados e passaram.


## Subgate 06g — fechamento por execução portátil

Foi adicionado um workflow focado dormant por padrão:
`.github/workflows/focused-portable.yml`.

Ele dispara somente quando `.github/focused-validation.trigger` muda e, portanto, não adiciona
custo aos pushes normais. O job usa Ubuntu 24.04, configura
`NRFUSION_FOCUSED_NR_SESSION_VALIDATION=ON`, compila somente os dois targets de NrSession e executa
o regex focado do CTest.

### Diagnóstico medido

Run `35722332627`:
- compile/link dos dois targets: PASS;
- `nrfusion_nr_session_tests`: PASS;
- stress: FAIL por allocations.

Run `35722518049`:
- allocation localization: 2 allocations;
- ambas em `Resolve`;
- primeiro frame: 780;
- Begin/Submit/Map/Retire: 0 allocations.

Run `35722745314`:
- tamanhos: 80 B e 80 B;
- causa identificada nos dois windows de 5 doubles do controller.

`RobustNrEstimate` fazia `push_back` com o vector já cheio e só depois removia o elemento antigo.
Com capacity 5, o sexto push fazia growth para capacity 10: exatamente 80 bytes. Isso acontecia em
`nrRecentSamples_` e `frameRecentSamples_`.

Correção:
- quando o window está cheio, remove o elemento antigo **antes** do `push_back`;
- capacity 5 nunca é excedida;
- algoritmo, tamanho da janela e ordem lógica das amostras permanecem iguais.

Como `PerformanceController.cpp` era um arquivo legado >300, a correção também fechou a dívida
estrutural em vez de manter o arquivo grandfathered:
- `PerformanceController.cpp`: 284 linhas, hot update/window logic;
- `PerformanceControllerLifecycle.cpp`: 209 linhas;
- `PerformanceControllerScale.cpp`: 56 linhas.

As ações de scale transition foram separadas por responsabilidade; o steady `Update`,
`CanScaleDown/Up`, EWMA, budget e robust window permanecem no mesmo TU hot.

### Validação final

Run `35723235377`, head `e2697cc283a144f79109fd7faab0247d155d11c8`:
- configure focado: PASS;
- build `nrfusion_nr_session_tests`: PASS;
- build `nrfusion_nr_session_stress_tests`: PASS;
- `nrfusion_nr_session_tests`: PASS;
- `nrfusion_nr_session_stress_tests`: PASS;
- 1.000.000 frames medidos;
- gate de allocation exige exatamente 0 e passou;
- sem Windows.

## Fechamento da Fase 06

Validated code commit after post-close adversarial review: `916190b95f50b9f54d1af320db30495ede87c317`.

Call graph standalone final:
`carrier/provider -> FrameContract/NrSessionFramePacket -> NrSession -> FusionRuntime policy +
fixed work/timing`.

A Fase 07 pode iniciar sem pendência funcional ou estrutural da Fase 06.


## Revisão adversarial pós-fechamento

Foi encontrado um invariant não coberto pelo fechamento 06g: o mesmo work submetido podia ser
inserido duas vezes no timing ring. A segunda completion já era rejeitada, porém a entrada duplicada
consumia capacidade e podia deslocar/abandonar outro work válido sob pressão.

Correção:
- cada work possui um bit fixo `timingMapped`;
- `MarkTimingMapped` aceita exatamente uma associação após Submit;
- mapping repetido falha antes de tocar o timing ring;
- sem heap, lock, lookup adicional ou mudança na completion normal;
- regressão explícita exige que o segundo `MapTimedWork` retorne false.

A mesma revisão removeu um `nrRecentSamples_.clear()` e um `emaNrMs_=0` duplicados literalmente
no branch de downshift do hot path; comportamento permanece idêntico.


### Validação da revisão pós-fechamento

Run `35724258682`, head `916190b95f50b9f54d1af320db30495ede87c317`:
- configure focado: PASS;
- build dos dois targets NrSession: PASS;
- `nrfusion_nr_session_tests`: PASS;
- `nrfusion_nr_session_stress_tests`: PASS;
- stress de 1.000.000 frames preserva 0 allocations;
- duplicate timing mapping regression: PASS;
- sem Windows.

A Fase 06 permanece CLOSED após a segunda revisão adversarial.


## Auditoria final 06h — cobertura dos gates

A revisão independente pós-fechamento identificou três lacunas de prova e as fechou sem mudar policy:

- o stress agora exige observar HybridNvfp4, mudança de working scale e mudança de execution generation;
- o timing ring possui regressão explícita 16 -> 17, provando displacement/abandon do work mais antigo;
- o workflow focado também compila e executa `nrfusion_tests`, preservando as regressões históricas de
  `PerformanceController`/`FusionRuntime` além do diferencial interno de `NrSession`.

Run `35783094348`, head `6fcdec5f53244d6912837f37f8b60c969d173425`:
- configure: PASS;
- build `nrfusion_nr_session_tests`: PASS;
- build `nrfusion_nr_session_stress_tests`: PASS;
- build `nrfusion_tests`: PASS;
- CTest: 3/3 PASS;
- stress medido: 1.000.000 frames, 0 allocations, com transitions obrigatoriamente observadas;
- nenhum Windows gate.

A Fase 06 está CLOSED sem ressalva de validação portátil.


## Cross-check Windows real ? 2026-09-23

A Fase 06 n?o tinha gate Windows espec?fico, mas seu conjunto inteiro foi inclu?do na auditoria
Windows real do head 1e6f55a:

- nrfusion_nr_session_tests: PASS;
- nrfusion_nr_session_stress_tests: PASS;
- nrfusion_tests (controller/runtime regressions): PASS;
- MinGW x64 full CTest: 35/35 PASS;
- MSVC x64 full CTest: 35/35 PASS.

Isso n?o altera o crit?rio original da fase; apenas confirma que os invariants port?teis permanecem
v?lidos sob os dois toolchains Windows usados na auditoria.
