# Fase 04 — Evidência do NGX feature registry

## Boundary

Arquivos principais:

- `include/nrfusion/NgxFeatureRegistry.hpp`;
- `src/NgxFeatureRegistry.cpp`;
- `tests/ngx_feature_registry_tests.cpp`.

O registry é portátil e não inclui headers NGX/vendor.

A Fase 05 possui o feature lifecycle do executor e a Fase 07 integra o registry no carrier.
A Fase 04 entrega somente o boundary portátil e seus invariantes.

## Classificação verificada

`NgxFeatureCreateEvent` recebe o feature ID bruto do `CreateFeature`.

| NGX feature ID | Kind |
|---:|---|
| 1 | SuperResolution |
| 11 | FrameGeneration |
| 13 | RayReconstruction |
| 18 | Unknown |
| qualquer outro | Unknown |

Os IDs 1/11/13 foram conferidos no enum público `NVSDK_NGX_Feature` do header NVIDIA/DLSS.
O fixture NRFusion usa feature 18 para o modelo NR privado; a revisão adicionou regressão para
mantê-lo Unknown/PassThrough e evitar auto-interceptação.

Nenhum parâmetro de feature é lido para inferir kind.

## Lifecycle

Cada create bem-sucedido gera `NgxFeatureToken` com raw handle, context ID e generation.

Failed create, handle zero ou context zero não alteram o registry.

A revisão encontrou que o código original da Fase 04 aceitava um segundo create bem-sucedido no
mesmo `context+handle` ainda ativo e substituía silenciosamente a identidade anterior. Isso podia
mascarar release perdido ou ordenação inválida.

Agora create duplicado ativo falha fechado e preserva a identidade existente.

Reuse válido segue:

`create(old) → release(old token) → create(new same raw handle) → new generation`

Depois do reuse, repetir o token antigo falha e não remove a geração nova.

`Clear()` remove slots, mas não reinicia generation.

## ABI real e raw handle

A revisão comparou o registry com o ABI NGX real:

- `D3D12_CreateFeature` recebe feature ID e retorna `NVSDK_NGX_Handle**`;
- `D3D12_EvaluateFeature` usa o feature handle;
- `D3D12_ReleaseFeature` recebe somente `NVSDK_NGX_Handle*`.

Por isso o registry agora oferece:

- `Lookup(contextId, handle)` para owner que conhece contexto;
- `LookupUnique(handle)` para raw-handle boundary;
- `ActionFor(handle)` para evaluate fail-closed sem contexto.

Se o mesmo raw handle estiver ativo em mais de um contexto, `LookupUnique` retorna missing e
`ActionFor(handle)` retorna PassThrough.

Generation-safe release continua recebendo `NgxFeatureToken`. O ABI NGX não fornece generation no
release, portanto o carrier futuro precisa reter o token devolvido no create junto do owner/wrapper
da feature. O registry sozinho não consegue distinguir um evento stale de uma geração antiga se o
owner perdeu essa associação e só apresenta novamente o mesmo ponteiro bruto.

Essa limitação agora é explícita; a evidência anterior sobre release era mais forte do que o ABI
permitia sustentar.

## Evaluate gate

| Kind/lookup | Action |
|---|---|
| SuperResolution | NeuralRendering |
| RayReconstruction | NeuralRendering |
| FrameGeneration | PassThrough |
| Unknown | PassThrough |
| feature 18 privada | PassThrough |
| missing | PassThrough |
| raw handle ambíguo | PassThrough |

O fake executor incrementa NR somente para SR/RR.

## Storage / custo

- capacidade fixa: 64 slots;
- storage: `std::array`;
- heap interno: nenhum;
- lock interno: nenhum;
- context lookup: scan máximo de 64 slots;
- raw unique lookup: scan máximo de 64 slots;
- create/release: bounded pela mesma capacidade.

## Testes da revisão

Além da suíte original:

- create duplicado ativo é rejeitado e identidade anterior permanece;
- reuse só ocorre após release;
- stale token após reuse é rejeitado;
- raw unique lookup resolve handle único;
- raw handle duplicado entre contextos resulta em PassThrough;
- raw-handle lookup/action entra no loop de 1.000.000 operações allocation-free;
- feature privada 18 permanece Unknown/PassThrough.

## Source size após revisão

| Arquivo | Linhas |
|---|---:|
| `NgxFeatureRegistry.hpp` | 81 |
| `NgxFeatureRegistry.cpp` | 122 |
| `ngx_feature_registry_tests.cpp` | 197 |

Nenhum bloco de comentário novo/tocado ultrapassa 120 caracteres.

## Validação final da revisão

Head de código validado: `3f883b1ecfc39a3eb921d578b8686159e1c30be0`.

Portable Core run `35639114955`:

- build warnings-as-errors: PASS;
- `nrfusion_ngx_feature_registry_tests`: PASS, 0,11 s;
- CTest portable: 6/6 PASS.

Windows run `35639114971`:

- MSVC build: PASS;
- `nrfusion_ngx_feature_registry_tests`: PASS, 0,15 s;
- CTest Windows: 19/19 PASS;
- integrated OptiScaler distribution: PASS;
- NSIS/public developer dist: PASS;
- workflow: PASS.

## Commits da revisão

- `741894a3698b3c458a07053716022d4e4e38a3f2` — reject duplicate active create;
- `40297cf41f682cfdd6e218af1b21109b2dccb84f` — fail closed on ambiguous raw handles;
- `c515f2ad5374e2b2162f14d45b1315170b013cd7` — cover raw-handle hot path;
- `3f883b1ecfc39a3eb921d578b8686159e1c30be0` — pin private feature 18 to passthrough.

## Gate Fase 04

Fechado após revisão adversarial. Fase 05 não foi iniciada.
