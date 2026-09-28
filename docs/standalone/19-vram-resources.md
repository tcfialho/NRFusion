# Fase 19 — Disciplina de recursos e VRAM

## Status

**CLOSED em `e132c6ea1eefeaddf0f6c36582c021c865c3b3e6`.**

Objetivo concluído: lazy allocation, ownership explícito e retirement seguro sem introduzir
um ResourceManager global. O histórico detalhado de subgates, commits, runs e checkpoints está
em `docs/standalone/19-vram-resources-history.md`.

## Implementação

- [x] Manter resource ledger: size formula, owner, trigger, reuse, release e resize.
- [x] Multipass/residual/hold/diagnostics off não mantêm extras first-party controlados.
- [x] Evitar duplicação carrier/executor.
- [x] Separar persistent/transient/vendor-owned.
- [x] Liberar gerações antigas após retirement.
- [x] Counters de bytes/resources onde controlamos allocation.
- [x] Ledger/pool por domínio; nenhum ResourceManager multifunção >300 linhas.

## Revisão obrigatória

- [x] Resize/toggle/failure não acumulam generations.
- [x] Shared resources contam no orçamento.
- [x] Não trocar VRAM por CPU sem tradeoff no hot path.
- [x] Ownership continua local ao subsistema que usa o recurso.

## Validação rápida

- [x] Resize/toggle/failure loops.
- [x] Resource counts por feature controlada.
- [x] Hardware real mede peak final.
- [x] LOC checker.

## Gate

- [x] Sem leak/recurso inativo conhecido nos owners auditados.
- [x] VRAM alvo <= baseline equivalente.
- [x] Código de resource ownership <=300 por arquivo modificado/split nesta fase.

## Resource ledger final

| Domínio | Owner | Trigger / footprint | Reuse | Release |
| --- | --- | --- | --- | --- |
| Scratch NR | `D3D12NrScratchResources` | core + opcionais por formato/tamanho | desc/usage compatíveis | opcionais por usage; gerações via retirement |
| Guide clones | `D3D12NrGuideClones` | até depth+motion tipados | mesma desc tipada | resize/formato/direct-guide via retirement |
| Codec | `D3D12NrCodec` | 48 x 256 B + 1 heap/384 descriptors | ring 48 slots | `Shutdown()` |
| Timing | `D3D12RetiredTimingSource` | query heap + 16 timestamps readback | ring 8 samples | `ResetAfterIdle()` |
| Diagnostics | `NrD3D12Diagnostics` | 256 KiB readback + 32768 queries | somente frame diagnóstico | read terminal após fence |
| Synthetic D3D12 | `SyntheticDx12Provider` | 3 RGBA16F por slot + descriptor heap | ring 3 slots | resize/shutdown; init failure limpa |
| D3D11 bridge | `SyntheticDx11BridgeProvider` | ring de color/residual compartilhados | geração atual | resize/shutdown; publicação atômica |
| OpenGL bridge | `SyntheticOpenGlProvider` | D3D12/GL shared slots + fences | geração atual | resize/shutdown; bootstrap atômico |
| Vulkan import | `SyntheticVulkanProvider` | image/memory importados quando usados | owner nativo | failure destrói/free; shutdown destrói/free |
| Host64 guides | `HostServer64` | depth+motion zero + staging transitório | mesma resolução | staging após guide fence; guides após imports/fences |
| Capture32 color | `D3D11CaptureSession` | shared color/output + scaling pipeline | geração de swapchain | `InvalidateLocked()` em resize/failure/shutdown |
| Capture32 depth | `D3D11CaptureSession` | SRV + R16F shared depth + UAV/shader/constants | geração de transport | publicação atômica; partial failure destrói temporários |
| Vendor hold | runtime/fixture externo | `heldColor` pertence ao caminho vendor | fora do owner standalone | separado do orçamento first-party |

## Fechamento técnico

### Retirement e accounting

- `fe3a595`: scratch opcional segue usage efetivo e aposenta extras.
- `c050a50` / `7b30eaf`: scratch ativo + aposentado entra no accounting.
- `e945699` / `98931f0`: guide clones entram no accounting e clones stale são aposentadas.
- `ea67c56`: codec mantém 48 slots, mas usa um único descriptor heap.
- `145951f`: diagnostics libera query/readback após fence e recria limpo.
- `a140aef`: synthetic D3D12 reporta resources/bytes/heaps ativos.

### Host64 e failure paths

- `c9504e9` / `8884173`: staging e zero-guides inativos são aposentados por fences.
- `8d1d3ee`: `Stop()` libera GPU owners só após idle marker concluído.
- `8c8812e`: init Host64 publica device/queue/allocators/fence/providers atomicamente.
- `0fe7ceb`: D3D11 bridge + Synthetic D3D12 limpam/publikam init sem partial ownership.
- `f76db8a`: bootstrap D3D12 do OpenGL tornou-se atômico.
- `b55c668`: Capture32 foi dividido; principal 93 linhas, fragments 192/188/173/200/202/291.
- `e132c6e`: depth opcional do Capture32 só publica a geração completa; falhas fecham handle
  e destroem SRV/shared texture/UAV/shader/constants temporários.

### Validação final do code head

`e132c6e`:
- Portable `36366804431`: PASS.
- Focused Portable `36366804475`: PASS.
- Windows `36366804444`: PASS; 33/33 testes executados passaram.
- Windows incluiu `nrfusion_capture32_transport_timeout_tests` PASS em 0,32 s e
  `nrfusion_ipc_host_test` PASS em 0,40 s.
- Focused checkpoint artifact `10947318107`,
  sha256 `8eaab2d813847326200debb8e7fd1cc964a81d9de9e16af72c6c87798e1baeae`.
- `check_source_size.py changed`: PASS / zero violações após o split Capture32.

### Gate físico de VRAM

Mesmo RTX 4050 Laptop 6 GB, mesmo workload `nrfusion_ipc_host_test`,
`GPU Process Memory\\Dedicated Usage` por PID durante hold local idêntico de 5 s:

- baseline pré-Phase-19 `6f597f4`: **92,254 MiB em 3/3**;
- code head final `e132c6e`: **92,254 MiB em 3/3**;
- delta: **0 MiB**; todos os seis workloads concluíram PASS.

A instrumentação de medição foi local, idêntica nos lados comparados e revertida após a coleta.

## Limitações conhecidas fora do gate de ownership

- Hosted OpenGL external interop é SKIP no runner. No PC, `provider=0` foi reproduzido
  igualmente antes/depois de `f76db8a`; raw recreation/reuse passam. É um problema funcional
  preexistente, não evidência de retention introduzida pela Phase 19.
- `nrfusion_capture32_d3d11_hook_test` no PC não chegou a configurar o capture client mesmo
  antes de `e132c6e`; portanto não foi usado como prova do depth failure-path. O owner depth
  é validado por publicação atômica, build Windows, source-size gate e ausência de partial state.

## Próxima fase

Fase 20.
