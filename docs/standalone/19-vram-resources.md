# Fase 19 — Disciplina de recursos e VRAM

## Objetivo

Garantir lazy allocation e resource ownership explícito sem criar um ResourceManager gigante.

## Dependências

Contínua; consolidar após features principais.

## Fora de escopo

- Prometer economia sem medir
- Alocar preventivamente

## Implementação

- [ ] Manter resource ledger: size formula, owner, trigger, reuse, release e resize.
- [ ] Multipass/residual/hold/diagnostics off não mantêm extras.
- [ ] Evitar duplicação carrier/executor.
- [ ] Separar persistent/transient/vendor-owned.
- [ ] Liberar gerações antigas após retirement.
- [ ] Counters de bytes/resources onde controlamos allocation.
- [ ] Ledger/pool por domínio; nenhum `ResourceManager` multifunção >300 linhas.

## Revisão obrigatória

- [ ] Resize/toggle/failure não acumulam generations.
- [ ] Shared resources contam no orçamento.
- [ ] Não trocar VRAM por CPU sem tradeoff.
- [ ] Ownership continua local ao subsistema que usa o recurso.

## Validação rápida

- [ ] Resize/toggle/failure loops.
- [ ] Resource counts por feature.
- [ ] Hardware real mede peak final.
- [ ] LOC checker.

## Gate

- [ ] Sem leak/recurso inativo.
- [ ] VRAM alvo <= baseline equivalente.
- [ ] Código de resource ownership <=300 por arquivo.

## Próxima fase

Fase 20.


## Subgate 19a — retirement de scratch opcional

Auditoria:
- `D3D12NrScratchResources` já possuía `Retire(kind)`, mas o executor só fazia
  `EnsureOptional()` quando a feature estava ativa.
- Ao voltar multipass/reduced/crop/residual para off, os recursos opcionais continuavam
  residentes até resize completo ou shutdown.
- `PassScratch` também era alocado por `requestedPasses > 1` mesmo quando
  `PreparePassFeatures()` reduzia o número efetivo de passes para 1.

Correção:
- `D3D12NrScratchUsage` descreve apenas necessidade de scratch, sem virar manager global.
- `RetireUnused()` faz preflight da capacidade da retirement queue e aposenta de forma
  fail-closed os opcionais inativos.
- `PassScratch` agora segue `effectivePasses > 1`.
- `ColorSmall` segue reduced; `ActiveColor` segue crop; os quatro recursos residual
  seguem `acrossRr`.
- `OutputNative` não é requerido pelo executor atual e qualquer instância stale é aposentada.
- Retirement continua deferred; nenhum recurso em voo é liberado diretamente.

Validação:
- `nrfusion_nr_scratch_resources_tests` cobre retire por usage e contagem da retirement queue.
- O teste foi incluído no Windows fast gate, não apenas compilado.
- Portable `36324845068`: PASS.
- Focused Portable `36324845106`: PASS.
- Windows `36324845150`: PASS.
- Checkpoint `nrfusion-source-fe3a595db19b7f2fd57326715089ebbffa6eefac`,
  artifact `10933437248`,
  sha256 `81bc41d6ea7ad43586fea26b91db1072e2445267c0ddb889a78a0af637d8628e`.
- Código validado: `fe3a595db19b7f2fd57326715089ebbffa6eefac`.

**Subgate 19a CLOSED. Fase 19 permanece IN PROGRESS.**

## Subgate 19b — accounting de residency controlada

Auditoria e correção:
- `c050a50` adicionou `resourceCount` e `logicalBytes` do scratch ativo em owner
  separado; `c1f8052` corrigiu a compilação MSVC sem mudar a semântica.
- `7b30eaf` passou o tamanho lógico dos scratch resources para a retirement queue.
  Recursos aposentados continuam contabilizados até `Tick()`/`DrainAfterIdle()` liberá-los.
- `e945699` estendeu a mesma disciplina para depth/motion guide clones, incluindo
  accounting ativo e bytes aposentados.
- `98931f0` corrigiu o caso typeless -> typed: quando o caller volta a usar o guide
  original, a clone stale é aposentada em vez de ficar residente indefinidamente.
- Accounting permanece control-plane only: sem device query, heap allocation, mutex ou GPU work.

Validação:
- Portable `36352523502`: PASS.
- Focused Portable `36352523438`: PASS.
- Windows `36352523339`: PASS.
- O Windows push gate agora executa explicitamente
  `nrfusion_nr_guide_clones_tests` e `nrfusion_d3d12_nr_codec_tests`.
- No run `36352523339`, guide clones passou em 0,06 s e codec em 0,26 s.
- Checkpoint `nrfusion-source-98931f07c8ddc46c30f02bff45ea5381074b006c`,
  artifact `10942138379`,
  sha256 `e10d18105ea991bb311265859ab85a51b4ab1f16c3908ce6c8d88e456a82df12`.

**Subgate 19b CLOSED.**

## Subgate 19c — footprint persistente do codec

Auditoria:
- `D3D12NrCodec` usa 48 slots para evitar sobrescrever constants/descriptors ainda em voo.
- Cada slot mantém um constant buffer upload de 256 bytes: 48 recursos, 12 KiB lógicos.
- O desenho anterior criava também 48 shader-visible descriptor heaps separados.

Correção:
- `ea67c56` consolidou os 48 heaps em um único heap com 384 descriptors.
- Cada slot mantém uma região independente de 8 descriptors; a proteção contra reuse em voo
  continua sendo o ring de 48 slots.
- `D3D12NrCodec::Accounting()` reporta 48 resources, 12 KiB e 1 descriptor heap após
  `Init()`, e zero após `Shutdown()`.
- O teste executa dois dispatches consecutivos para exercitar mais de uma região do heap.

**Subgate 19c CLOSED. Fase 19 permanece IN PROGRESS.**

## Resource ledger atual

| Domínio | Owner | Trigger | Footprint controlado | Reuse | Release/resize |
| --- | --- | --- | --- | --- | --- |
| Scratch NR | `D3D12NrScratchResources` | primeiro frame/feature necessária | core + opcionais por formato/tamanho; bytes e count ativos | reutiliza enquanto desc/usage compatíveis | opcionais por usage; gerações antigas via retirement |
| Guide clones | `D3D12NrGuideClones` | guide typeless que exige formato tipado | até depth + motion; bytes/count ativos | reutiliza enquanto desc tipada coincide | resize/formato ou retorno ao guide direto via retirement |
| Codec | `D3D12NrCodec` | primeiro `ExecuteMainFrame` que inicializa codec | 48 x 256 B upload + 1 heap de 384 descriptors | ring fixo de 48 slots | `Shutdown()`; não depende de resize |
| Timing D3D12 | `D3D12RetiredTimingSource` | `BindAfterIdle()` explícito | readback de 16 timestamps + query heap | ring fixo de 8 samples | `ResetAfterIdle()` |
| Diagnostics legado | `NrD3D12Diagnostics` | `NRFusion_BeginNrDiagnosticFrame` | readback de 256 KiB + query heap de 32768 timestamps | existe somente durante frame diagnóstico | libera após `ReadNrDiagnosticFrame` com fence concluído |
| Synthetic D3D12 | `SyntheticDx12Provider` | primeiro uso de cada ring slot | 3 texturas RGBA16F por slot + 1 descriptor heap | ring fixo de 3 slots; reutiliza até resize | `Shutdown()`; accounting reporta bytes/count ativos |

## Subgate 19d — diagnostics off sem GPU resources residentes

Correção `145951f`:
- `Prepare()` cria query heap/readback em temporários e só publica o par completo.
- Após o fence comprovar conclusão, `NRFusion_ReadNrDiagnosticFrame` libera query heap,
  readback e referência ao device em sucesso ou falha terminal.
- Accounting expõe 2 objetos GPU + 256 KiB enquanto diagnostics está ativo e zero após leitura.
- `tests/nvapi_stub/nvapi.h` existe somente para compilar esse owner no hosted Windows;
  não altera o header/runtime NVAPI usado pelo patcher.
- O harness executa dois ciclos begin -> fence -> read e exige recriação limpa em ambos.

Validação:
- Portable `36353342312`: PASS.
- Focused Portable `36353342318`: PASS.
- Windows `36353342324`: PASS; `nrfusion_d3d12_diagnostics_resources_tests` PASS em 0,03 s.
- Checkpoint `nrfusion-source-145951f558046c7ba88e12fd46bd99aa3ecb6902`,
  artifact `10942253939`,
  sha256 `73bc01564241712f427ef6a58e3e87bb10381639c5e70869432132098284739e`.

**Subgate 19d CLOSED.**

## Subgate 19e — accounting do synthetic D3D12

Correção `a140aef`:
- `SyntheticDx12Provider::Accounting()` contabiliza os recursos ativos do ring sem alocar,
  consultar device ou emitir GPU work.
- O footprint atual é 3 texturas RGBA16F por slot usado:
  `3 * width * height * 8` bytes lógicos por slot.
- O scale gate prova dois slots ativos após 64x64 + 32x32:
  6 resources, 122880 bytes lógicos, 1 descriptor heap.
- Após `Shutdown()`, resources/heaps/bytes retornam a zero.

Validação:
- Portable `36353705045`: PASS.
- Focused Portable `36353705106`: PASS.
- Windows `36353705052`: PASS; `nrfusion_synthetic_dx12_scale_gate_test` PASS em 0,11 s.
- Checkpoint `nrfusion-source-a140aef29ac47800b68f648adc9c2695c0a922b3`,
  artifact `10943172628`,
  sha256 `ec0528af05b88de79511f4a8970fac3599b483839b876848cfade5dd8acbdf42`.

**Subgate 19e CLOSED. Fase 19 permanece IN PROGRESS.**

## Auditoria dos demais carriers hosted

- OpenGL: resize só recria depois que slots em voo aposentam; `CloseSharedHandles()` libera
  D3D12/GL resources antes da nova resolução.
- Vulkan: `ImportD3D12Resource()` e `ImportD3D12Fence()` não possuem callsite no source/test
  Vulkan atual; não há import repetitivo ativo para corrigir.
- D3D11: carriers standalone usam resources do jogo; o bridge próprio mantém fences e
  `ResetAfterIdle()` explícito.

## Blocker atual

O próximo recurso inativo concreto está no Host64: `zeroGuideUpload_` é staging temporário
para inicializar depth/motion zero, mas `EnsureZeroGuides()` retorna cedo em resolução estável
sem liberá-lo mesmo após `guideFence_` concluir.

Próxima correção: aposentar `zeroGuideUpload_` assim que o guide upload fence estiver
concluído, sem wait, preservando `lowGuideDepth_`/`lowGuideMotion_`. Cobrir no hosted
Host64/IPC antes de qualquer gate físico.

A medição física de peak VRAM final continua pendente e não deve ser substituída por hosted CI.
