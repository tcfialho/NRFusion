# Fase 04 — Evidência do NGX feature registry

## Boundary

Novos arquivos:

- `include/nrfusion/NgxFeatureRegistry.hpp`;
- `src/NgxFeatureRegistry.cpp`;
- `tests/ngx_feature_registry_tests.cpp`.

O registry é portátil e não inclui headers NGX/vendor.

Não existe hoje um callsite NGX first-party de produção onde instalar detours sem inverter a
arquitetura. A Fase 05 possui o feature lifecycle do executor e a Fase 07 explicitamente integra
o registry no D3D12 carrier. A Fase 04 entrega o boundary que esses owners consomem.

## Classificação

`NgxFeatureCreateEvent` recebe o feature ID bruto do `CreateFeature`.

Classificação usada:

| NGX feature ID | Kind |
|---:|---|
| 1 | SuperResolution |
| 11 | FrameGeneration |
| 13 | RayReconstruction |
| qualquer outro | Unknown |

Os IDs correspondem ao enum público `NVSDK_NGX_Feature` do header
`NVIDIA/DLSS/include/nvsdk_ngx_defs.h` revisado em 2026-09-21.

Nenhum parâmetro de feature é lido para inferir kind.

## Lifecycle

Cada create bem-sucedido gera `NgxFeatureToken` com:

- raw handle;
- context ID não-zero;
- generation monotônica não-zero.

Failed create, handle zero ou context zero não alteram o registry.

Reuse do mesmo context+handle substitui a identidade e recebe generation nova. Um release precisa
apresentar o token completo; generation antiga falha e não remove a feature atual.

`Clear()` remove slots, mas não reinicia a generation. Assim token de sessão anterior não pode
voltar a coincidir com um create posterior.

O mesmo raw handle em contextos diferentes ocupa identidades independentes.

## Evaluate gate

A única decisão do registry é:

| Kind | Action |
|---|---|
| SuperResolution | NeuralRendering |
| RayReconstruction | NeuralRendering |
| FrameGeneration | PassThrough |
| Unknown | PassThrough |
| handle ausente | PassThrough |

Logo o registry nunca autoriza NR para FG/Unknown.

O fake executor do teste só incrementa sua chamada NR quando `ActionFor()` retorna
`NeuralRendering`; SR e RR incrementam, FG/Unknown/missing não incrementam.

## Storage / custo

- capacidade fixa: 64 slots;
- storage: `std::array`;
- heap interno: nenhum;
- lock interno: nenhum;
- lookup worst-case: scan de 64 slots;
- create/release worst-case: scans bounded pela mesma capacidade.

O registry é estado de lifecycle com owner único. Se um carrier futuro receber callbacks
concorrentes, a serialização pertence ao owner do carrier; a Fase 04 não adiciona mutex ao hot path.

## Testes

`nrfusion_ngx_feature_registry_tests` cobre:

- failed create sem mutação;
- feature IDs SR/RR/FG/Unknown;
- SR+FG e RR+FG;
- passthrough de FG/Unknown/missing;
- failed recreate preservando identidade anterior;
- mesmo handle em contextos diferentes;
- reuse no mesmo context+handle;
- stale release rejeitado;
- double release rejeitado;
- `Clear()` sem reciclar generation;
- capacity completa e overflow fail-closed;
- 100.000 ciclos create/lookup/release;
- 1.000.000 lookups/actions com contador global de allocations;
- zero allocations no trecho steady-state.

## Source size

| Arquivo | Linhas |
|---|---:|
| `NgxFeatureRegistry.hpp` | 78 |
| `NgxFeatureRegistry.cpp` | 101 |
| `ngx_feature_registry_tests.cpp` | 176 |
| `NRFusionCore.cmake` | 87 |
| `NRFusionTests.cmake` | 19 |

Nenhum bloco de comentário novo/tocado ultrapassa 120 caracteres.

## Validação

Head de código validado: `3a9c037c0cecb7a598f05543019af3039d9981ea`.

Portable Core run `35633516316`:

- build warnings-as-errors: PASS;
- `nrfusion_ngx_feature_registry_tests`: PASS, 0,01 s;
- CTest portable: 6/6 PASS.

Windows run `35633516287`:

- MSVC build: PASS;
- `nrfusion_ngx_feature_registry_tests`: PASS, 0,02 s;
- CTest Windows: 19/19 PASS;
- integrated OptiScaler distribution: PASS;
- NSIS/public developer dist: PASS;
- workflow: PASS.

## Gate Fase 04

Fechado. Fase 05 não foi iniciada.
