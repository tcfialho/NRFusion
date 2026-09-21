# NRFusion Universal Standalone

## Missão

Transformar o NRFusion em um host universal de DLSS 5 sem depender do OptiScaler e sem exigir DLSS nativo no jogo.

Alvos:

- x64 e x86;
- D3D12, D3D11, D3D10, D3D9, Vulkan e OpenGL;
- providers Native, Bridge e Synthetic;
- Neural Rendering sempre que existir uma rota tecnicamente válida;
- MFG qualificado separadamente;
- executor DLSS 5 D3D12 x64 canônico sempre que possível.

**Target não significa Supported.** Cada rota só vira suportada após passar seu gate de qualificação.

## Arquitetura

```text
Game API / bitness
      ↓
Acquire
      ↓
Normalize -> Universal FrameContract
      ↓
NrSession
      ↓
Execute -> Canonical D3D12 DLSS 5
      ↓
Compose / interop de volta ao jogo
```

Cada carrier qualifica Acquire, Normalize, Execute e Compose separadamente. Um SyntheticProvider que aceita `ResourceRef` não prova aquisição em jogos reais.

## Regra estrutural: máximo 300 linhas

Regra imediata para o standalone: todo arquivo first-party handwritten novo ou substantivamente modificado deve ter **<=300 linhas físicas**, contando comentários e linhas em branco. Alvo prático: <=250.

Escopo: C/C++/headers, CUDA, shaders, Python, PowerShell, CMake/build logic, installer, tests, harnesses e tools.

Transição:

- legado first-party >300 pode permanecer **read-only/no-growth** até a fase dona;
- ao precisar evoluí-lo, fazer split mecânico primeiro ou aposentá-lo na mesma fase;
- cutover final: **zero first-party handwritten >300**.

Exceções só para:

- docs;
- generated reproduzível por generator/input tracked, marcado generated e nunca editado manualmente;
- vendor/third-party e fixtures upstream enquanto permanecerem sem modificação local.

Regras anti-evasão:

- dividir por ownership/lifetime/responsabilidade, nunca `Part1/Part2`;
- não espalhar um God class pelos arquivos para cumprir número;
- não minificar, empilhar statements, esconder código em strings/`.inc`, ou mover implementação para headers;
- modificação handwritten em vendor/fixture deixa de ser exceção.

Toda fase herda esse gate.

## Reuso antes de criar

Primeiro estender `nrfusion_sim`, `nrfusion_harness_3d`, synthetic tests e IPC/capture32/Host64 tests existentes.

## Contrato de performance

Steady state normal:

```text
heap allocations/frame          = 0
GPU resource creation/frame     = 0
descriptor/query heap/frame     = 0
filesystem/string parsing/frame = 0
shader compilation/frame        = 0
blocking GPU wait/frame         = 0
```

Também:

- evitar mutex no normal path sem concorrência real;
- Diagnostics/Advanced off não consomem GPU work/VRAM exclusivo;
- VRAM equivalente <= baseline atual salvo tradeoff medido;
- medir p50/p95/p99;
- melhora de média com piora de p99 é regressão até análise;
- ganho real de FPS/GPU só é afirmado com hardware real.

## Estratégia de validação

1. **CPU/fake path** — policy, lifecycle, IPC e failure injection.
2. **Harness gráfico** — Acquire/Normalize/Execute/Compose determinísticos.
3. **Jogos reais** — driver, aquisição real, imagem, VRAM e performance final.

Antes de jogo real, tentar reproduzir no menor harness existente.

## Ordem

| Fase | Tema |
|---|---|
| 00 | Baseline |
| 01 | FrameContract |
| 02 | Runtime shell |
| 03 | Development Harness |
| 04 | Feature registry |
| 05 | D3D12 executor |
| 06 | NrSession |
| 07 | D3D12 carrier |
| 08 | Timing/Diagnostics |
| 09 | D3D11 carrier |
| 10 | x86 + Host64 |
| 11 | Vulkan |
| 12 | OpenGL |
| 13 | D3D10 |
| 14 | D3D9 |
| 15 | Guide acquisition |
| 16 | MFG |
| 17 | Menu/config |
| 18 | Compatibility |
| 19 | VRAM/resources |
| 20 | Hot-path + structural audit |
| 21 | Executor optimization |
| 22 | Qualification |
| 23 | Cutover |

## Regras de execução

- O arquivo da fase atual é a fonte de verdade.
- Não avançar sem gate anterior ou blocker explícito.
- Cada item deve ser pequeno e verificável.
- Toda fase fecha somente se código novo/tocado respeita <=300 linhas.
- Estabilizar lote antes de mover branch/rodar CI completo.
- CI confirma integração; não substitui revisão.
- Mudança hot-path registra antes/depois/custo/comportamento.
- Barrier, ownership, lock e fallback exigem justificativa.

## Arquivos

- [00 — Baseline](00-baseline.md)
- [01 — FrameContract](01-frame-contract.md)
- [02 — Runtime shell](02-runtime-shell.md)
- [03 — Development Harness](03-development-harness.md)
- [04 — Feature registry](04-feature-registry.md)
- [05 — D3D12 executor](05-d3d12-executor.md)
- [06 — NrSession](06-nr-session.md)
- [07 — D3D12 carrier](07-d3d12-carrier.md)
- [08 — Timing/Diagnostics](08-timing-diagnostics.md)
- [09 — D3D11 carrier](09-d3d11-carrier.md)
- [10 — x86 + Host64](10-x86-host64.md)
- [11 — Vulkan](11-vulkan-carrier.md)
- [12 — OpenGL](12-opengl-carrier.md)
- [13 — D3D10](13-d3d10-carrier.md)
- [14 — D3D9](14-d3d9-carrier.md)
- [15 — Guide acquisition](15-guide-acquisition.md)
- [16 — MFG](16-mfg.md)
- [17 — Menu/config](17-menu.md)
- [18 — Compatibility](18-compatibility.md)
- [19 — VRAM/resources](19-vram-resources.md)
- [20 — Hot-path audit](20-hotpath-audit.md)
- [21 — Executor optimization](21-executor-optimization.md)
- [22 — Qualification](22-qualification.md)
- [23 — Cutover](23-cutover.md)

## Critério final

NRFusion só substitui OptiScaler quando as rotas anunciadas qualificarem Acquire→Normalize→Execute→Compose, o host tiver menor overhead equivalente, não houver regressão de VRAM equivalente e houver zero first-party handwritten code file >300 linhas.
