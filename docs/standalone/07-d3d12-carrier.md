# Fase 07 — D3D12 x64 carrier

## Status

**CONCLUÍDA ESTRUTURALMENTE — 07a–07i validados portavelmente; gate Windows/hardware diferido para o cutover global.**

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose e decompor o provider atual por ownership real.

## Dependências

Fases 04–06.

## Fora de escopo

- Outras APIs
- Universalizar MFG

## Implementação

- [x] Evoluir o carrier standalone sem splitar o legado antes do cutover; provider/Host64/patcher permanecem read-only.
- [x] Ownership separado em Acquire/Normalize/Session/Execute/Compose/capabilities, reutilizando os owners de GPU da Fase 05.
- [x] Definir Acquire seam/lifetime de color/depth/motion/exposure.
- [x] Usar FrameContract com evidence/provenance explícitas; guides selecionados precisam corresponder à fonte decidida.
- [x] Integrar registry/NrSession; Provider anuncia Acquire/Normalize e Executor só ativa após qualificação.
- [x] Resize/device rebind/guides ausentes cobertos estruturalmente; Windows compile permanece diferido.
- [x] Disabled quase pass-through.
- [x] Cada arquivo novo/tocado do carrier <=300 linhas.

## Revisão obrigatória

- [x] Provider aceitar ResourceRef não prova Acquire.
- [x] Nenhum CPU pixel path.
- [x] Scratch/guide owners retornam sem criação quando desc/shape coincide; codec slots são criados no Init.
- [x] Descriptor writes/copies são explícitos no executor canônico; carrier não adiciona locks nem heaps por frame.
- [x] Novo carrier foi separado por Acquire/Normalize/Session/Execute/capabilities, sem split textual do legado.

## Validação rápida

- [x] Regressões portáteis cobrem a rota nova; split do legado não foi realizado por decisão explícita de cutover.
- [x] Harness portátil cobre Acquire facts/normalização/identity; harness COM/Windows fica no gate final.
- [x] LOC checker dos arquivos novos/tocados em 07a–07h.
- [ ] Jogos reais permanecem no gate de cutover/hardware e não bloqueiam o fechamento estrutural.

## Gate

- [x] Acquire, Normalize, Execute e Compose têm owners separados.
- [x] Carrier steady-state não cria heap/resource/descriptor heap quando device/shape permanecem estáveis.
- [x] D3D12 carrier <=300 por arquivo.

## Próxima fase

Fase 08.


## Auditoria inicial — 2026-09-22

Estado atual:
- `include/nrfusion/SyntheticDx12Provider.hpp`: 103 linhas;
- `src/SyntheticDx12Provider.cpp`: 596 linhas, ainda grandfathered e read-only;
- testes D3D12 existentes: `nrfusion_synthetic_dx12_test`,
  `nrfusion_synthetic_dx12_scale_gate_test` e harness D3D12;
- boundaries do monólito confirmados: initialization/shaders, slot resources,
  submit/extract e compose/poll.

O split direto do provider não é seguro enquanto o fallback OptiScaler estiver ativo:
`tools/apply_to_optiscaler.py` possui 3900 linhas e copia nominalmente apenas
`SyntheticDx12Provider.cpp`. Dividir o source sem atualizar esse manifest quebraria o fallback;
tocar o patcher agora também violaria a regra estrutural, porque ele é um arquivo handwritten >300
e a Fase 23 manda removê-lo, não refatorá-lo prematuramente.

Portanto nenhum source D3D12 foi alterado neste subgate. O blocker é de integração/ownership, não de
implementação do carrier.

## Próxima ação exata

Definir uma migração que permita ao standalone sair do monólito sem quebrar o fallback legado e sem
modificar o patcher >300. Só depois executar o split mecânico e comparar os testes atuais antes/depois.


### Couplings adicionais do audit

- `src/HostServer64.cpp`: 659 linhas; trocar o tipo de provider diretamente exigiria tocar outro
  arquivo grandfathered cuja decomposição pertence à Fase 10.
- `tests/synthetic_dx12_test.cpp`: 418 linhas; não deve ser expandido nesta fase sem split.
- `tests/synthetic_dx12_scale_gate_test.cpp`: 269 linhas e pode servir como primeiro gate Windows
  quando o novo carrier tiver uma boundary testável.

A rota segura para a próxima sessão é tratar o novo carrier standalone como substituição progressiva,
mantendo `SyntheticDx12Provider`, Host64 e patcher legados read-only enquanto o novo ownership é
estabelecido. Nenhum wrapper que apenas delegue ao monólito conta como fechamento da Fase 07.


## Subgate 07a — contrato portátil de Acquire

Foram adicionados:
- `D3D12CarrierContract.hpp`: contrato de snapshot/result sem ponteiros de API;
- `D3D12CarrierContract.cpp`: normalização para `FrameContext`;
- `d3d12_carrier_contract_tests.cpp`: regressões portáteis.

A boundary separa recurso meramente mencionado de recurso declarado como adquirido. Um `ResourceRef`
válido com `acquired=false` é rejeitado; recursos adquiridos exigem evidence explícita e
`sourceFrameId` da frame atual. Color é obrigatório e precisa ter a resolução de render.

Esse subgate define o seam, mas não transforma o bit `acquired` em prova nativa por si só. O próximo
native owner deve ser o único produtor normal do snapshot e derivar metadata do próprio
`ID3D12Resource`.

Validação:
- run `35784800058`: PASS;
- contrato compila no core portátil com warnings-as-errors;
- regressões de missing/unproven/stale/evidence/size/jitter: PASS.

## Subgate 07b — Acquire -> NrSession

`D3D12CarrierSession` passou a possuir a transação de policy da rota D3D12 sem depender de
`SyntheticDx12Provider`:
- snapshot válido -> `FrameContext` -> `NrSessionFramePacket` -> `NrSession::Resolve`;
- Acquire rejeitado não muta state/telemetry da sessão;
- `NotConfigured` e `Disabled` não tentam Acquire;
- generation stale é rejeitada antes de Acquire;
- `GameContext` não-D3D12 é rejeitado antes de Acquire;
- nenhuma dessas preflights toca resource, heap ou lock.

A ordem é deliberada para o próximo native seam: resource D3D12 de uma generation antiga nunca deve
chegar a `GetDesc()` depois de reconfigure.

Validação final do subgate:
- run `35785029967`: Acquire->NrSession inicial PASS;
- run `35785318761`: inactive bypass PASS;
- run `35785412711`: stale-before-Acquire PASS;
- run `35785546335`, head `1dbb83ee017b5596f966c47fd7f13aea1a4bcf2b`: 5/5 PASS;
- `nrfusion_d3d12_carrier_contract_tests`: PASS;
- `nrfusion_d3d12_carrier_session_tests`: PASS;
- Fase 06 regressions permanecem no mesmo gate;
- nenhum Windows gate foi usado.

Tamanhos do código novo/tocado:
- `D3D12CarrierContract.hpp`: 59;
- `D3D12CarrierContract.cpp`: 119;
- `D3D12CarrierSession.hpp`: 42;
- `D3D12CarrierSession.cpp`: 46;
- `d3d12_carrier_contract_tests.cpp`: 141;
- `d3d12_carrier_session_tests.cpp`: 109;
- CMake core/tests: 114/103;
- workflow focado: 51.

Os três legados continuam byte-untouched pela Fase 07:
`SyntheticDx12Provider.cpp`, `HostServer64.cpp` e `apply_to_optiscaler.py`.

## Próxima ação exata após 07b

Criar o native Acquire owner Windows-only em arquivos novos <=300. Ele deve receber
`ID3D12Resource*`, consultar `GetDesc()`, derivar resolução/formato do recurso real e produzir o
snapshot consumido por `D3D12CarrierSession`. Não aceitar dimensões/formato de color fornecidos pelo
caller como verdade.

Somente depois desse owner existir:
1. registrar capabilities reais da rota D3D12;
2. ligar work identity ao `D3D12NrExecutor`/scratch já existentes;
3. provar resize/device removal/guides ausentes;
4. decidir a retirada do monólito legado sem tocar prematuramente no patcher.


## Subgate 07c — native Acquire, registry e work identity

A prova nativa foi separada em duas camadas:

- `D3D12CarrierNativeFacts` é portátil e valida facts de textura, evidence e lifetime;
- `D3D12CarrierNativeAcquire` é Windows-only e recebe `ID3D12Resource*`, chama `GetDesc()`
  e produz os facts consumidos pela camada portátil.

O caller não fornece largura, altura ou formato de color. Esses dados vêm do resource real.
A camada de facts exige Texture2D, array size 1, mip count 1 e sample count 1 para os recursos
capturados. Color/guides precisam de formato representável pelo `FrameContract`.

Output é diferente: ele fornece `outputResolution`, mas seu formato não precisa ser representável
pelo core. Isso evita rejeitar swapchains válidos só porque o enum portátil ainda não representa
todos os formatos DXGI.

O owner Windows faz preflight de identity, color/output ausentes e evidence antes de chamar
`GetDesc()`. O source foi adicionado apenas ao ramo `WIN32` de `nrfusion_core`; não houve
Windows build nesta sessão, portanto sua compilação Windows permanece um gate futuro, conforme a
política do repositório.

### Registry

O provider D3D12 registra somente as capabilities já implementadas:
- Acquire;
- Normalize.

`Execute` e `Compose` existem como bits reservados, mas não são registrados nem anunciados.
Assim o registry não antecipa suporte inexistente.

### Work identity

`D3D12CarrierSession` agora possui a transação de work:
- `BeginWork` cria `WorkTicket` pelo `NrSession`;
- `submissionEpoch == WorkTicket.id`;
- Submit/Abandon/MapTimedWork/RetireTimedInterval continuam delegados ao owner único `NrSession`;
- work de generation antiga falha fechado após reconfigure;
- IDs continuam monotônicos entre session resets.

Isso prepara o pending-submission gate do `D3D12NrExecutor` sem criar um segundo namespace de
epochs.

### Validação 07c

Run final `35796387110`, code head
`23465328c4a36433d6bbde993d2493a46fb8ca91`:
- focused configure/build: PASS;
- CTest: **8/8 PASS**;
- `nrfusion_d3d12_carrier_contract_tests`: PASS;
- `nrfusion_d3d12_carrier_session_tests`: PASS;
- `nrfusion_d3d12_carrier_native_facts_tests`: PASS;
- `nrfusion_d3d12_carrier_capabilities_tests`: PASS;
- `nrfusion_d3d12_carrier_work_tests`: PASS;
- regressões da Fase 06 permanecem no mesmo gate;
- source checkpoint artifact: PASS;
- nenhum Windows gate.

Arquivos principais atuais:
- `D3D12CarrierNativeFacts.hpp/cpp`: 61 / 103;
- `D3D12CarrierNativeAcquire.hpp/cpp`: 40 / 91;
- `D3D12CarrierCapabilities.hpp/cpp`: 25 / 19;
- `D3D12CarrierSession.hpp/cpp`: 59 / 70;
- testes novos 07c: 126 / 45 / 101 linhas;
- CMake core/tests: 117 / 106;
- workflow focado: 53.

O diff da sessão não toca `SyntheticDx12Provider.cpp`, `HostServer64.cpp` nem
`apply_to_optiscaler.py`.

## Próxima ação exata após 07c

Criar a boundary Windows-only de execução do carrier:
1. receber `D3D12CarrierWork` + frame normalizada;
2. preencher `D3D12NrFramePlanInput` e `D3D12NrFrameRequest`;
3. passar `work.submissionEpoch` ao `D3D12NrExecutor`;
4. reutilizar os owners de scratch/retirement da Fase 05;
5. manter `Execute` e `Compose` fora do registry até cada caminho existir e possuir regressão.

Não tocar o provider/Host64/patcher legados para fazer essa ligação.


## Subgate 07d — execution boundary

O carrier agora possui uma boundary de execução separada do executor canônico:

- `D3D12CarrierExecutionPlan` valida frame/work/guides/placement antes de qualquer chamada D3D12;
- working scale, reset, HDR e submission epoch vêm do estado já normalizado/`NrSession`;
- `PreSr` e `PostSr` são aceitos;
- `AcrossRr` e `DeferredResidual` permanecem fail-closed até existir o seam de compose pareado;
- `D3D12CarrierExecutor` recebe resources nativos, verifica identidade contra Acquire e então chama
  `D3D12NrExecutor::ExecuteFrame`;
- scratch, guide clones, retirement, feature lifetime e barriers continuam owned pelo executor da Fase 05.

A auditoria encontrou e corrigiu:
- output identity estava sendo descartada após Acquire;
- HDR podia vir de uma segunda fonte de verdade;
- game exposure ausente podia avançar quando ambos os lados estavam nulos;
- dois campos do contrato foram temporariamente parar nos structs errados; o run de recovery recompilou
  o lote completo após a correção.

Recovery portátil:
- run `35809871923`: SUCCESS;
- execution plan + regressões anteriores: PASS.

## Subgate 07e — resize quarantine e device rebind

Resize real de render/output já muda a structural identity no `FusionRuntime`, avançando
`runtimeGeneration`. A regressão do carrier agora prova que:
- work criado antes do resize não pode mais ser submetido nem mapeado para timing;
- o work antigo ainda pode ser abandonado para liberar o slot;
- um `D3D12CarrierFrameResult` anterior ao resize não pode iniciar novo work;
- o próximo work usa a generation nova.

Run `35810072641`: **9/9 PASS**, incluindo
`nrfusion_d3d12_carrier_execution_plan_tests` e a regressão de resize em
`nrfusion_d3d12_carrier_work_tests`.

Para device recreation/removal, `D3D12CarrierExecutor` agora possui lifecycle explícito:
- `BindDeviceAfterIdle(ID3D12Device*)`;
- `ShutdownAfterIdle()`;
- mudança de device derruba o executor anterior antes de recarregar NGX no novo device;
- o carrier mantém uma referência COM ao device bound, evitando confundir um novo device com um
  endereço reciclado;
- `Execute` falha fechado enquanto nenhum device estiver bound.

Esse trecho é Windows-only e **não foi compilado neste gate portátil**. A semântica `AfterIdle` é
intencional: `D3D12NrExecutor::Shutdown` libera feature/scratch/retirement assumindo GPU ociosa.

Tamanhos atuais do 07d/07e:
- `D3D12CarrierExecutionPlan.hpp/cpp`: 61 / 112;
- `d3d12_carrier_execution_plan_tests.cpp`: 161;
- `D3D12CarrierExecutor.hpp/cpp`: 80 / 133;
- `d3d12_carrier_work_tests.cpp`: 114.

`Execute` continua **não registrado** no capability registry porque a boundary Windows ainda não
teve compile gate. `Compose` também permanece não registrado.

## Próxima ação exata após 07e

Qualificar Compose sem duplicar o provider legado:
1. documentar/encapsular que o executor canônico já faz resolve/compose para PreSr/PostSr;
2. definir o seam separado necessário para AcrossRr/DeferredResidual;
3. somente anunciar Execute/Compose depois que cada rota tiver evidência suficiente;
4. manter `SyntheticDx12Provider.cpp`, `HostServer64.cpp` e `apply_to_optiscaler.py` read-only.


## Subgate 07f — ownership de Compose e contrato de guides

O carrier não ganhou um segundo compositor. A revisão do executor canônico confirmou:

- PreSr/PostSr já fazem resolve/compose dentro de `D3D12NrExecutor::ExecuteFrame`;
- AcrossRr já possui protocolo pareado interno:
  - estágio store executa pre-RR, preserva residual/history e não escreve o resultado final;
  - estágio apply executa post-RR e consome o residual armazenado;
  - ambos usam o mesmo `submissionEpoch`;
- `ApplyStoredResidual` falha fechado se o epoch não corresponder;
- `DeferredResidual` não possui modo canônico equivalente e continua não suportado no carrier.

O seam portátil agora modela explicitamente:
- `Direct`;
- `AcrossRrStore`;
- `AcrossRrApply`.

O carrier deriva `runBeforeUpscale`, `rayReconstruction` e `residualAcrossRr`; esses três
flags não são mais confiados ao caller. O apply stage não exige color/depth/motion/exposure porque o
executor canônico consome apenas output + residual previamente armazenado nesse ponto.

### Guide contract

Antes de executar um model stage, o plano agora exige:
- depth presente e `DepthReliable()`;
- motion presente;
- `PipelineDecision.motion` limitado a Native ou DlssContract;
- provenance/reliability do recurso de motion compatível com a fonte realmente selecionada.

Isso impede que a policy selecione uma fonte e o executor receba silenciosamente outra.

O primeiro run do guide gate (`35814648573`) falhou apenas porque o fixture não inicializava
`PipelineDecision.motion` e portanto herdava `Zero`. O fixture foi corrigido sem relaxar o
contrato.

Validação final:
- run `35814760066`: **SUCCESS**;
- **9/9 focused tests PASS**;
- paired AcrossRr store/apply: PASS;
- DeferredResidual fail-closed: PASS;
- missing/unreliable depth: PASS;
- missing/mismatched/unsupported motion source: PASS;
- Native e DlssContract motion coerentes: PASS.

Code head validado:
`27bdbfa095b4265376d063bddf31af70fc04a69e`.

Tamanhos:
- `D3D12CarrierExecutionPlan.hpp`: 77;
- `D3D12CarrierExecutionPlan.cpp`: 165;
- `d3d12_carrier_execution_plan_tests.cpp`: 227;
- `D3D12CarrierExecutor.cpp`: 137.

### Blocker encontrado para o próximo subgate

`D3D12CarrierNativeAcquire::MapFormat` ainda retorna `ResourceFormat::Unknown` para guides
D3D12 typeless. Isso entra em conflito com a Fase 05, cujo executor já sabe clonar formats typeless
para views typed em `D3D12NrExecutorFramePrepare.cpp`.

Portanto o próximo trabalho não é Compose. É alinhar o native Acquire com essa capacidade sem mentir
sobre o formato portátil:
1. definir normalização semântica testável para depth/motion typeless;
2. manter color/output com regras próprias;
3. não aceitar metadata de formato inventada pelo caller;
4. somente depois revisar se Execute/Compose podem ser anunciados no registry.


## Subgate 07g — guides typeless e ownership de capabilities

O blocker de guides typeless foi fechado sem aceitar metadata inventada pelo caller.

Foi introduzida uma normalização portátil por **papel** e **família typeless**:
- depth aceita apenas famílias compatíveis com depth que o executor da Fase 05 sabe clonar;
- motion aceita apenas famílias compatíveis com motion;
- famílias de outro papel retornam `ResourceFormat::Unknown`;
- color/output não passam por essa exceção guide-only.

Os formatos semânticos necessários para representar o clone typed foram acrescentados ao
`ResourceFormat`, preservando explicitamente os valores numéricos 0–7 já existentes. A regressão
trava essa ABI para impedir deslocamento silencioso futuro.

O adapter Windows agora:
- classifica `DXGI_FORMAT_*_TYPELESS` por família;
- aplica a normalização apenas em depth/motion;
- continua derivando tudo de `ID3D12Resource::GetDesc()`;
- não recebe formato do caller.

A regressão final também prova que os formatos semânticos normalizados atravessam
`NativeFacts -> D3D12AcquireSnapshot -> FrameContract`.

### Capability ownership

O registry agora possui duas identidades separadas:
- Provider D3D12: `Acquire | Normalize`;
- Executor D3D12: `Execute | Compose`.

O componente Executor é apenas uma identidade/factory neste momento. Ele **não é registrado
automaticamente em produção**; isso evita anunciar a rota Windows antes do gate de integração.
O teste prova que registrar explicitamente esse componente produz a máscara correta e que Provider
não herda bits de Execute/Compose.

### Validação 07g

- run typeless inicial `35818039759`: SUCCESS;
- run capability ownership `35818221637`: SUCCESS;
- run final `35818315400`, code head
  `993f440b227f88f0a08923bb97f035719fda147c`: SUCCESS;
- focused suite: 10/10 PASS;
- `nrfusion_d3d12_guide_format_tests`: PASS;
- normalized typeless guides através do FrameContract: PASS;
- legacy numeric `ResourceFormat` values: compile-time locked;
- todos os arquivos tocados nesta sessão <=300 linhas.

### Auditoria steady-state

O carrier novo não adiciona criação por frame:
- `D3D12NrScratchResources::Ensure/EnsureOptional` retornam imediatamente para desc/shape estáveis;
- `D3D12NrGuideClones::Ensure` retorna para clone com o mesmo desc;
- heaps/constant buffers do codec são criados em `D3D12NrCodec::Init`;
- descriptor writes do codec continuam por dispatch e estão explicitamente contabilizados;
- nenhum lock foi introduzido pelo carrier.

## O que ainda impede fechar a Fase 07

A implementação estrutural está pronta, mas a fase continua **EM ANDAMENTO** por dois itens de
integração, não por falta de boundary:

1. `D3D12CarrierNativeAcquire.cpp` e `D3D12CarrierExecutor.cpp` continuam Windows-only e não
   tiveram compile/harness Windows nesta fase, conforme a política atual de diferir Windows;
2. o componente Executor existe no registry, mas ainda não é ativado pelo bootstrap/runtime real.

O legado `SyntheticDx12Provider` continua intencionalmente read-only para não quebrar o fallback
OptiScaler/pat​​cher antes do cutover.

## Próxima ação exata após 07g

Criar a integração de bootstrap do carrier sem anunciar executor prematuramente:
1. definir um plano D3D12 que registre Provider sempre e Executor somente depois de bind/init bem-sucedido;
2. preservar rollback fail-closed se o executor não inicializar;
3. testar a lógica de ativação com um estado/fake portátil, sem COM/Windows;
4. manter o caminho Windows real diferido para o gate de cutover;
5. depois reavaliar se a Fase 07 pode ser fechada estruturalmente e entrar na Fase 08.


## Subgate 07h — bootstrap activation e fechamento estrutural

A ativação do carrier foi ligada ao runtime sem expor o registry mutável:

- `StartD3D12CarrierRuntime` inicia o shell com o componente Provider D3D12;
- `RuntimeBootstrap::ActivateComponent` permite ativação bounded de um componente após o bootstrap;
- `ActivateD3D12CarrierExecutor` só tenta o Executor quando o runtime está Running e o Provider está ativo;
- falha de qualificação mantém o runtime Provider-only;
- falha de qualificação chama rollback explícito para desfazer bind/init parcial;
- sucesso registra `Execute | Compose`;
- ativação repetida é idempotente e não requalifica;
- runtime Disabled/Stopped não tenta qualificar GPU.

O primeiro run do subgate (`35819463575`) falhou somente porque a edição do workflow deixou a regex
do CTest sem fechar e duplicou package/upload. O YAML foi restaurado em commit isolado, sem alterar
o código do carrier.

O run `35819575671` validou o lote anterior, mas a revisão de ancestry mostrou que os commits finais
de rollback estavam dois commits à frente. Por isso foi executado um gate adicional no head correto.

Validação final:
- run `35819742596`: **SUCCESS**;
- code head validado: `c11e42bdf808dc4b0bcf54e50afa985ca2cd8113`;
- focused suite incluindo `nrfusion_d3d12_carrier_bootstrap_tests`: PASS;
- rollback após qualification failure: PASS;
- Provider-only após failure: PASS;
- Provider + Executor após success: PASS;
- nenhum Windows gate intermediário foi usado.

### Fechamento da Fase 07

A rota standalone D3D12 agora tem owners explícitos para:
- Acquire;
- Normalize;
- policy/session/work identity;
- Execute;
- Compose direto e Across-RR pareado;
- capabilities/bootstrap activation;
- resize generation quarantine e device rebind lifecycle.

O que permanece diferido não é dívida estrutural desta fase:
- compile/harness Windows do native Acquire/Executor;
- jogos reais;
- substituição física do `SyntheticDx12Provider`/Host64/patcher no cutover.

Esses itens permanecem no gate global de integração/hardware definido pelo repositório.

## Próxima ação

Entrar na **Fase 08 — Timing e Diagnostics desacoplados**. Primeiro auditar o timing existente
(`TimingWorkMapper`, telemetry e diagnostics) e definir um owner portátil de timing aposentado
sem waits, sem duplicar o `WorkLedger`/`NrSession`.


## Subgate 07i — adversarial hardening pós-review

A revisão pós-fechamento encontrou cinco invariants que ainda podiam falhar. Foram corrigidos antes
de iniciar a Fase 08.

### Guide role

Formats typed agora passam pela mesma validação por papel usada para typeless:
- depth aceita somente formatos depth compatíveis;
- motion aceita somente formatos motion compatíveis;
- um formato conhecido mas pertencente ao papel errado vira `ResourceFormat::Unknown`;
- regressão explícita rejeita RG16 como depth e D32 como motion.

### Work liveness e execução única

O executor não confia mais apenas nos campos copiáveis de `D3D12CarrierWork`:
- `NrSessionWorkTracker` sabe se o ticket ainda está Started;
- resize/reconfigure/abandon/submit invalidam execution eligibility;
- `ClaimExecution` consome um claim exatamente uma vez;
- duas chamadas Execute com o mesmo ticket não podem gravar GPU duas vezes;
- o claim só é consumido depois de plan/device/resource preflight.

### Device identity

O wrapper Windows verifica que command list, output e todos os resources presentes pertencem ao
mesmo `ID3D12Device` bound no executor antes de delegar ao executor canônico.

### Submission semantics

`D3D12CarrierExecuteResult` separa:
- `Applied()`: resultado final foi aplicado;
- `NeedsSubmission()`: a command list precisa avançar, incluindo `PendingFeature`.

O operador booleano segue `NeedsSubmission()`, evitando descartar a criação de feature que precisa
ser submetida antes da próxima epoch.

### Capability deactivation

O registry agora permite remover capability bits sem derrubar o runtime inteiro.
`DeactivateD3D12CarrierExecutor`:
- remove `Execute | Compose` primeiro;
- executa o rollback/teardown depois;
- preserva o Provider;
- é idempotente quando o Executor já está ausente;
- permite requalificar e reativar depois.

`StartD3D12CarrierRuntime` também permanece idempotente depois que o Executor foi ativado.

O teardown automático por destrutor não foi adotado: `ShutdownAfterIdle` libera owners de GPU que
exigem idle explícito. O lifecycle correto é deactivation -> rollback/`ShutdownAfterIdle`.

### Validação 07i

Run `35824485857`, code head
`071d63834b2303878a93866a996c06693784a404`:
- configure/build focado: PASS;
- CTest: **11/11 PASS**;
- guide role regression: PASS;
- work liveness + one-shot claim: PASS;
- capability deactivate/reactivate: PASS;
- regressões anteriores de Fases 06/07: PASS;
- nenhum Windows gate intermediário.

Todos os arquivos first-party tocados no 07i permanecem abaixo de 300 linhas.
A Fase 07 retorna a CLOSED após esta revisão adversarial.
