# Fase 00 — Evidência de baseline

## Revisões congeladas

- NRFusion master: `410cab8ab1876b7ae7dbf0a82b1b4096b225f4f3`
- Plano standalone: `b76238022838d246f7fce5695fe321ec232a840b`
- OptiScaler upstream: `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass`
- OptiScaler commit: `1b1dd650d35ea59ea2d1d0bf7937b71645159f75`

## Distribuição atual

`tools/build_dist.ps1` ainda:

1. cria worktree do OptiScaler pinned;
2. executa `tools/apply_to_optiscaler.py`;
3. compila o forwarder DLSS-NR;
4. compila `OptiScaler.sln`;
5. empacota `OptiScaler.dll`, forwarder e subtree do OptiScaler;
6. compila Host64/capture32 separadamente.

O installer usa `OptiScaler.dll` como proxy x64. Em x86 usa `nrfusion_capture32.dll` e
`NRFusionHost64.exe`.

## Responsabilidades hoje injetadas no OptiScaler

O patch toca diretamente:

- config/persistência do NR e MFG;
- `DlssNrFeature_Dx12` e `DlssNrFeature_Vk`;
- `DlssNrNative` e o executor `DlssNr_Dx12`;
- exposure scan e menu NR;
- swapchain synthetic path;
- Vulkan hooks;
- Streamline hooks;
- library-load hooks;
- MFG unlock/Ampere loader;
- menu MFG.

Destino standalone:

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
| Vulkan | 11 |
| OpenGL | 12 |
| D3D10 | 13 |
| D3D9 | 14 |
| guides/exposure | 15 |
| MFG/Streamline/DLSSG | 16 |
| menu/config UI | 17 |
| compatibility | 18 |
| resources/VRAM | 19 |
| hot path | 20 |
| kernel/executor optimization | 21 |
| qualification | 22 |
| installer/build cutover | 23 |

## Capability atual

`GameProbe::IntegratedCapabilities()` registra:

- native provider: presente;
- bridge provider: presente;
- synthetic D3D12: presente;
- synthetic D3D11 bridge: presente;
- synthetic Vulkan: presente;
- x86 carrier: presente;
- OpenGL carrier: ausente.

Limites atuais:

- x86 é D3D11-only;
- OpenGL não possui hook real;
- D3D9/D3D10 são explicitamente unsupported;
- presença de provider não equivale a Hardware-qualified.

## Call path e overhead observável

Caminho atual, simplificado:

```text
game
 -> OptiScaler hooks / patched feature or swapchain
 -> patched DLSS-NR host path
 -> OptiScalerAdapter
 -> FusionRuntime/controller/policies
 -> DLSS-NR execution + resolve/compose
```

O MFG usa Streamline/library-load/MfgUnlock integrados ao `DlssgTransfusion`.

Fatos estáticos no baseline:

- `OptiScalerAdapter.cpp`: 843 linhas;
- 45 `std::scoped_lock(mutex_)` explícitos;
- patch possui call sites separados para ResolveAuto, Begin/Submit work, timing retirement e telemetry;
- D3D12 NR mantém timer de intervalo total e timer do modelo;
- o patch adiciona timer de resolve;
- `GpuTime_Dx12` usa timestamp begin/end + ResolveQueryData e faz Map do readback ao consumir resultado.

Esses números são baseline estrutural, não medição de FPS.

## Recursos D3D12 atuais

O `NrState` upstream possui estes grupos de `ID3D12Resource*`:

- `colorCopy`, `output`: staging principal;
- `passScratch`: multipass;
- `hdrCopy`, `activeColor`;
- `colorSmall`: working scale < 1;
- `outputNative`: supersampling down-leg;
- `residualEdited`, `residualHistory[2]`, `residualComposed`;
- `heldColor`: frame hold;
- `meter`, `meterReadback[4]`: exposure;
- `calib`, `calibReadback[4]`: calibration;
- `depthClone`, `motionClone`, `depthConstant`: guide compatibility/fallback.

Também existem features por pass e scalers `superUp/superDown`. O standalone deve preservar
semântica/lifetime antes de tentar reduzir recursos.

## Dívida de source-size

Inventário completo do baseline: **34 arquivos first-party handwritten >300 linhas**.

| Arquivo | Linhas | Destino |
|---|---:|---|
| `tools/apply_to_optiscaler.py` | 3900 | retirar F23 |
| `tests/controller_tests.cpp` | 2743 | split F20/22 |
| `src/CaptureD3D11.cpp` | 1302 | split F09/10 |
| `src/AdaW4A8Interceptor.cpp` | 1097 | split F05/21 |
| `tests/harness_3d/D3D12TestHarness.cpp` | 1085 | split F03 |
| `src/cuda/W4A8FfnSm89.cu` | 946 | split F05/21 |
| `src/OptiScalerAdapter.cpp` | 843 | retirar F06 |
| `src/ProfileStore.cpp` | 666 | split se mantido F06/20 |
| `src/HostServer64.cpp` | 659 | split F10 |
| `tests/capture32_roundtrip_test.cpp` | 655 | split F10/20 |
| `tools/requiem_game/main.cpp` | 643 | decidir split/retire F03 |
| `src/GameProbe.cpp` | 628 | split F02 |
| `src/InstallerState.cpp` | 613 | split se mantido F23 |
| `src/SyntheticDx12Provider.cpp` | 596 | split F07 |
| `src/DlssgTransfusion.cpp` | 555 | split F16 |
| `tests/game_probe_tests.cpp` | 553 | split F02/20 |
| `tools/benchmark_sm89_ffn.cu` | 553 | split/retire F21 |
| `src/PerformanceController.cpp` | 514 | split F06/20 |
| `tests/residual_gpu_test.cpp` | 497 | split F03/20 |
| `installer/NRFusion.nsi` | 466 | split F23 |
| `tools/quantize_w4a8_sm89.py` | 423 | split se mantido F21 |
| `tests/synthetic_dx12_test.cpp` | 418 | split F07/20 |
| `src/SyntheticOpenGlProvider.cpp` | 409 | split F12 |
| `tools/extract_weights.py` | 402 | split/retire F21 |
| `tools/build_dist.ps1` | 395 | split F23 |
| `src/SyntheticVulkanProvider.cpp` | 377 | split F11 |
| `tools/decode_swin_abi.py` | 372 | split/retire F21 |
| `src/CaptureProvider32.cpp` | 370 | split F10 |
| `src/CompatibilityDatabase.cpp` | 350 | split F18 |
| `include/nrfusion/FusionRuntime.hpp` | 329 | split/retire F06 |
| `src/NrKernelAbi.cpp` | 318 | split F05/21 |
| `tests/w4a8_sm89_gpu_test.cu` | 313 | split F20/21 |
| `tests/capture32_d3d11_hook_test.cpp` | 312 | split F10/20 |
| `tests/ipc_host_test.cpp` | 301 | split F10/20 |

`CMakeLists.txt` tem 267 linhas: abaixo do hard cap, acima do soft target de ~250.

## Checker

`tools/check_source_size.py` possui 95 linhas e nenhuma dependência externa.

Validação em repositório Git temporário:

- `all`: reporta dívida legada e retorna sucesso;
- `changed`: árvore limpa retorna sucesso;
- `changed`: vendor modificado >300 retorna falha;
- `changed`: arquivo novo >300 retorna falha;
- `all --strict`: dívida existente retorna falha;
- `python -m py_compile`: sucesso.

O ambiente desta sessão não possui checkout local executável do NRFusion. Portanto o inventário acima
foi produzido pela API do GitHub, incluindo todos os arquivos candidatos e contagem física, enquanto o
comportamento do checker foi validado separadamente.


## Code review da Fase 00

A revisão do checker encontrou cinco problemas objetivos na primeira implementação:

1. `all --strict` podia ignorar vendor/third-party modificado já commitado;
2. a exceção de generated estava documentada, mas não implementada;
3. `.nsh`, `.psm1` e `.pyi` não eram classificados como source;
4. `all` ignorava arquivos source untracked;
5. um base Git ausente terminava em exceção/traceback em vez de erro curto.

Correções:

- `all` usa tracked + untracked;
- vendor/fixture só mantém isenção quando não mudou contra o base;
- generated requer diretório generated conhecido + marker `NRFUSION_GENERATED_FILE`;
- `master` tenta fallback para `origin/master`; base inexistente gera erro argparse;
- extensões previstas para installer/PowerShell/types Python entram no checker.

A reproducibilidade de generated continua sendo requisito de revisão: o checker valida localização/marker,
não tenta executar generators arbitrários.

`tests/test_source_size_checker.py` cobre vendor modificado, source untracked, generated válido,
marker falso fora de generated, `.nsh` e base inexistente.

Validação do conteúdo exato commitado:

- checker: 95 linhas;
- teste: 82 linhas;
- `python -S -m py_compile`: sucesso;
- regressão completa em repositório Git temporário: sucesso.

## Gate Fase 00

Fechado após code review. A próxima mudança funcional pertence à Fase 01.
