# Fase 00 — Evidência de baseline

## Revisões congeladas

- NRFusion master: `410cab8ab1876b7ae7dbf0a82b1b4096b225f4f3`
- Plano standalone: `b76238022838d246f7fce5695fe321ec232a840b`
- OptiScaler upstream: `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass`
- OptiScaler commit: `1b1dd650d35ea59ea2d1d0bf7937b71645159f75`

## Distribuição atual

`tools/build_dist.ps1` ainda cria o worktree do OptiScaler pinned, aplica o patch NRFusion,
compila forwarder + `OptiScaler.sln` e empacota o host atual. O installer usa `OptiScaler.dll`
como proxy x64; x86 usa capture32 + Host64.

## Responsabilidades hoje injetadas no OptiScaler

O patch cobre config/persistência, features NR D3D12/Vulkan, loader nativo, executor D3D12,
exposure, menu, swapchain synthetic, Vulkan hooks, Streamline, library-load e MFG.

| Responsabilidade | Fase |
|---|---:|
| runtime/bootstrap/config | 02 |
| harness | 03 |
| feature identity | 04 |
| executor/ABI DLSS-NR | 05 |
| policy/session | 06 |
| D3D12 acquire/compose | 07 |
| timing | 08 |
| D3D11 bridge | 09 |
| x86/Host64 | 10 |
| Vulkan/OpenGL/D3D10/D3D9 | 11–14 |
| guides/exposure | 15 |
| MFG | 16 |
| menu/config UI | 17 |
| compatibility | 18 |
| resources/VRAM | 19 |
| hot path | 20 |
| kernel/executor optimization | 21 |
| qualification | 22 |
| installer/build cutover | 23 |

## Capability atual

`GameProbe::IntegratedCapabilities()` registra native, bridge, synthetic D3D12, synthetic D3D11,
synthetic Vulkan e x86 carrier; OpenGL carrier está ausente. x86 é D3D11-only e D3D9/D3D10 são
legacy unsupported. Provider presente não significa Hardware-qualified.

## Call path e overhead observável

```text
game
 -> OptiScaler hooks / patched feature or swapchain
 -> patched DLSS-NR host path
 -> OptiScalerAdapter
 -> FusionRuntime/controller/policies
 -> DLSS-NR execution + resolve/compose
```

Baseline estático:

- `OptiScalerAdapter.cpp`: 843 linhas;
- 45 `std::scoped_lock(mutex_)` explícitos;
- host patch faz chamadas separadas de policy/work/timing/telemetry;
- NR D3D12 mantém timers total/model e o patch adiciona resolve timing;
- `GpuTime_Dx12` usa timestamps, resolve e readback map.

São fatos estruturais, não medição de FPS.

## Recursos D3D12 atuais

`NrState` contém staging principal, multipass, HDR/active color, working-scale/supersampling,
residual + history, hold-frame, exposure/calibration readbacks e clones/fallback de depth/motion.
A semântica/lifetime deve ser preservada antes de reduzir recursos.

## Dívida de source-size

Inventário completo do baseline: **34 arquivos first-party handwritten >300 linhas**.
O relatório detalhado está congelado no histórico desta fase; principais owners:

- patcher/OptiScalerAdapter: retirement F06/F23;
- harness/tests grandes: F03/F20/F22;
- D3D11/capture32/Host64: F09/F10;
- D3D12/Vulkan/OpenGL providers: F07/F11/F12;
- MFG: F16;
- controller/runtime/profile: F06/F20;
- CUDA/Ada/kernel tools: F05/F21;
- compatibility/build/installer: F18/F23.

`CMakeLists.txt` tem 267 linhas, abaixo do hard cap e acima do soft target.

## Checker revisado

A code review da Fase 00 encontrou e corrigiu cinco brechas:

1. `all --strict` ignorava vendor modificado já commitado;
2. a exceção generated estava documentada mas não implementada;
3. `.nsh`/PowerShell module não eram classificados como source;
4. `all` ignorava source untracked;
5. base Git ausente terminava em traceback pouco útil.

O checker agora:

- resolve `master` e `origin/master`, ou falha com erro curto;
- usa o diff contra base para retirar a isenção de vendor/fixture modificado;
- inclui tracked + untracked no modo `all`;
- cobre `.nsh`, `.psm1` e `.pyi`;
- só aceita generated marker dentro de diretórios generated definidos;
- permanece muito abaixo do cap de 300 linhas.

`tests/test_source_size_checker.py` preserva regressões de vendor, untracked, generated,
installer include e base inexistente.

O ambiente desta sessão não possui checkout executável do NRFusion. A lógica revisada foi exercitada
em repositório Git temporário; o inventário baseline veio da API do GitHub.

## Gate Fase 00

Fechado após code review. A próxima mudança funcional pertence à Fase 01.
