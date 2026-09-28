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
- [x] Hardware real mede peak final.
- [ ] LOC checker.

## Gate

- [ ] Sem leak/recurso inativo.
- [x] VRAM alvo <= baseline equivalente.
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
| Host64 zero guides | `HostServer64` | ausência de depth/motion importados | depth + motion RGBA16F e staging de upload temporário | par reutilizado na mesma resolução | staging após guide fence; par após imports substituírem fallback e fences concluírem |

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

## Subgate 19f — retirement dos zero guides Host64

Correção `c9504e9`:
- `zeroGuideUpload_` deixou de permanecer residente após a inicialização dos fallback guides.
- `CollectRetiredGuideUpload()` usa apenas `GetCompletedValue()`; não adiciona wait/event
  ao produto.
- A coleta ocorre no fluxo normal de frames e antes do early-return de resolução estável.
- O wait usado para provar retirement existe somente no harness WARP.
- O Windows push gate passou a executar `nrfusion_ipc_host_test`.

Validação:
- Portable `36354867650`: PASS.
- Focused Portable `36354867628`: PASS.
- Windows `36354867658`: PASS; `nrfusion_ipc_host_test` PASS em 0,39 s.
- Checkpoint `nrfusion-source-c9504e92316ba228f817c6caff0b544443e328fa`,
  artifact `10943502007`,
  sha256 `7661fdbef41a1ca551b01a01189e3c3b9fca7e581cb928edd829fd2efd52c7f1`.

Correção `8884173`:
- quando depth e motion importados tornam o fallback desnecessário,
  `CollectInactiveZeroGuides()` aposenta `lowGuideDepth_` e `lowGuideMotion_`;
- o release só ocorre após o fence de criação dos guides e o último host fence que usou o
  fallback estarem concluídos;
- o mesmo harness confirma que os dois fallback resources saem quando ambos os imports existem.

Validação:
- Portable `36355137406`: PASS.
- Focused Portable `36355137407`: PASS.
- Windows `36355137413`: PASS; `nrfusion_ipc_host_test` PASS em 0,44 s.
- Checkpoint `nrfusion-source-8884173d541ff2b8b911eb6817cd2ed4f716a14e`,
  artifact `10943254380`,
  sha256 `c736148bc6d8dfc6f0c093d6c3b3ffca7633ed7f2eb383b198077a87af4eec60`.

**Subgate 19f CLOSED. Fase 19 permanece IN PROGRESS.**

## Subgate 19g — cleanup GPU completo do Host64 em Stop

Correção `8d1d3ee`:
- `Stop()` não retorna mais cedo apenas porque `running_` já está false; cleanup é idempotente.
- Após a thread encerrar, um idle marker novo é enfileirado em `d3d12Queue_`/`d3d12Fence_`.
- GPU owners só são liberados quando esse marker realmente conclui; signal/event/timeout
  preservam os resources em vez de liberar objetos potencialmente em voo.
- O caminho idle libera transport imports, DLSS-NR executor, synthetic provider, zero guides,
  command lists/allocators/fences, queue e device, e zera o estado de fence/ring para restart.
- `nrfusion_host64_stop_cleanup_tests` mantém o objeto vivo após `Stop()` e exige zero GPU
  owners; uma segunda chamada a `Stop()` também permanece limpa.

Validação hosted:
- Portable `36357287344`: PASS.
- Focused Portable `36357287338`: PASS.
- Windows `36357287300`: PASS; `nrfusion_host64_stop_cleanup_tests` PASS em 0,08 s;
  33/33 testes executados passaram.
- Checkpoint `nrfusion-source-8d1d3eefe7b8ef82b796683f5e32e9dbf5c1912f`,
  artifact `10943849947`,
  sha256 `7adb6e5f21a1bf03820b0b3a83058e91e777c6f3d22a5c405f40e47c6337ac6e`.

Validação física local:
- Windows 11 / RTX 4050 Laptop GPU 6 GB, driver 617.14.
- `NRFUSION_TEST_D3D12_HARDWARE=1` força `D3D12TestDevice` a escolher adaptador
  high-performance não-software.
- `nrfusion_host64_stop_cleanup_tests` PASS em 0,62 s no hardware real.

**Subgate 19g CLOSED. Fase 19 permanece IN PROGRESS.**

## Subgate 19h — init Host64 atômico e gate físico de VRAM

Correção `8c8812e`:
- `InitializeD3D12()` cria device/queue/allocators/list/fence/providers em temporários e
  só publica o conjunto completo; falha intermediária não deixa estado parcial residente.
- Portable `36360933948`, Focused `36360933832` e Windows `36360934021`: PASS.
- Windows confirmou `nrfusion_ipc_host_test` em 0,41 s,
  `nrfusion_host64_stop_cleanup_tests` em 0,10 s e 33/33 executados PASS.
- Checkpoint artifact `10945422479`,
  sha256 `f84305d849171ced41cdc13d05811d9e8da91fde98b39dc83eca7c0ccf38e824`.

Gate físico no mesmo RTX 4050 Laptop 6 GB:
- baseline pré-Phase-19: `6f597f4`; code head final medido: `8c8812e`;
- mesmo `nrfusion_ipc_host_test`, com adaptação local idêntica para selecionar o adapter
  high-performance e hold de 5 s no mesmo ponto; edits revertidos após medir;
- `GPU Process Memory\\Dedicated Usage` por PID durante o hold:
  baseline 92,254 MiB em 3/3; atual 92,254 MiB em 3/3; delta 0 MiB;
- todos os seis runs do workload concluíram PASS.

**Gate físico de peak e `VRAM alvo <= baseline equivalente` CLOSED.**
**Subgate 19h CLOSED. Fase 19 permanece IN PROGRESS.**

## Subgate 19i — failure paths dos synthetic providers

Correção `0fe7ceb`:
- removeu quatro definições stale do bridge D3D11 que geravam `LNK4006` e mascaravam o
  source split; Windows final não contém mais esse warning;
- bootstrap D3D12 privado e publicação do ring D3D11 agora são atômicos; resize libera a
  geração antiga antes da tentativa, evitando peak duplo, e falha não publica slots parciais;
- falha de `Initialize()` do bridge limpa owners não submetidos;
- falha de init do `SyntheticDx12Provider` limpa fence, shaders/PSOs, heap, queue e device.
- Portable `36362975823`, Focused `36362975826`, Windows `36362975825`: PASS;
  Windows executou `nrfusion_synthetic_dx11_bridge_test` em 0,26 s; RTX 4050: 0,85 s.
- Checkpoint artifact `10946427117`,
  sha256 `cec7563559eb6d5ecdf4aa31fa9dc993d71228f230509b483d3cee685c6c4968`.

**Subgate 19i CLOSED. Fase 19 permanece IN PROGRESS.**

## Blocker atual

`SyntheticOpenGlProvider::CreatePrivateD3D12()` ainda publica device antes de queue,
allocators/fences/handles e retorna sucesso em retry apenas por `d3d12Device_ != nullptr`.
`CreateSharedResources()` já limpa falhas imediatamente e `Shutdown()` limpa estado parcial.

Próxima ação: tornar somente o bootstrap D3D12 privado OpenGL atômico e limpar init falho
antes de retry; validar os gates OpenGL hosted e, quando aplicável, no hardware local.
