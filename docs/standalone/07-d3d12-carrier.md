# Fase 07 — D3D12 x64 carrier

## Status

**EM ANDAMENTO — 07a–07d validados portavelmente; resize coberto, device rebind Windows implementado, Compose pendente.**

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose e decompor o provider atual por ownership real.

## Dependências

Fases 04–06.

## Fora de escopo

- Outras APIs
- Universalizar MFG

## Implementação

- [ ] Reusar provider/testes atuais, com split mecânico antes de evolução.
- [ ] Boundary sugerida pelo código atual: **initialization/shaders**, **slot resources**, **frame submit/extract**, **compose/poll**.
- [x] Definir Acquire seam/lifetime de color/depth/motion/exposure.
- [ ] Usar contrato DLSS/RR confiável ou Synthetic FrameContract honesto.
- [x] Integrar registry/NrSession para Acquire/Normalize; Execute/Compose ainda não são anunciados.
- [ ] Resize/device removal/guides ausentes.
- [x] Disabled quase pass-through.
- [ ] Cada arquivo <=300 linhas.

## Revisão obrigatória

- [x] Provider aceitar ResourceRef não prova Acquire.
- [x] Nenhum CPU pixel path.
- [ ] `EnsureSlotResources` só cria em init/reconfigure/resolution change.
- [ ] Descriptor writes/copies/locks são contabilizados.
- [ ] Split segue resource ownership, não ordem textual.

## Validação rápida

- [ ] Testes atuais antes/depois do split.
- [ ] Harness Acquire controlado.
- [x] LOC checker dos arquivos tocados em 07a–07c.
- [ ] Jogos reais só para Acquire/model final.

## Gate

- [ ] Quatro etapas têm owner.
- [ ] Fast path cumpre steady-state.
- [ ] D3D12 carrier <=300 por arquivo.

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
