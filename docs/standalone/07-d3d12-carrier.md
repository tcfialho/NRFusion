# Fase 07 — D3D12 x64 carrier

## Status

**EM ANDAMENTO — subgates 07a/07b concluídos; native Acquire/registry/executor ainda pendentes.**

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
- [ ] Integrar registry/NrSession. `NrSession` está integrado desde 07b; registry aguarda native owner real.
- [ ] Resize/device removal/guides ausentes.
- [x] Disabled quase pass-through.
- [ ] Cada arquivo <=300 linhas.

## Revisão obrigatória

- [ ] Provider aceitar ResourceRef não prova Acquire.
- [ ] Nenhum CPU pixel path.
- [ ] `EnsureSlotResources` só cria em init/reconfigure/resolution change.
- [ ] Descriptor writes/copies/locks são contabilizados.
- [ ] Split segue resource ownership, não ordem textual.

## Validação rápida

- [ ] Testes atuais antes/depois do split.
- [ ] Harness Acquire controlado.
- [ ] LOC checker.
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
