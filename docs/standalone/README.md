# NRFusion Universal Standalone

## Missão

Transformar o NRFusion em um host universal de DLSS 5 sem depender do OptiScaler e sem exigir DLSS nativo no jogo.

Alvos:

- x64 e x86;
- D3D12, D3D11, D3D10, D3D9, Vulkan e OpenGL;
- providers Native, Bridge e Synthetic;
- Neural Rendering sempre que existir uma rota tecnicamente válida;
- MFG qualificado separadamente, onde Streamline/DLSSG/presentation puderem ser integrados;
- executor DLSS 5 D3D12 x64 canônico sempre que possível.

**Target não significa Supported.** Cada rota só vira suportada após passar seu gate de qualificação.

## Arquitetura

```text
Game API / bitness
      ↓
Carrier / Provider
      ↓
Universal FrameContract
      ↓
NrSession
      ↓
Canonical D3D12 DLSS 5 Executor
      ↓
Compose / interop de volta ao jogo
```

A API determina **como** o frame chega ao NRFusion, não **se** o NR existe.

```text
contrato DLSS/RR utilizável -> Native/Bridge
sem contrato utilizável     -> Synthetic
```

## Reuso antes de criar

O projeto já possui peças de validação úteis. A primeira opção é estendê-las, não criar harness paralelo:

- `nrfusion_sim`: simulação CPU do controller;
- `nrfusion_harness_3d`: harness D3D12 headless com color/depth/motion/exposure e telemetry;
- `nrfusion_synthetic_dx12_test`;
- `nrfusion_synthetic_dx11_bridge_test`;
- `nrfusion_synthetic_opengl_test`;
- `nrfusion_ipc_host_test` e roundtrip do capture32/Host64.

Novo código de harness só é aceito quando essas peças não conseguem representar o cenário com uma extensão pequena.

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

Também é obrigatório:

- evitar mutex no caminho normal; lock exige concorrência real demonstrada;
- Diagnostics/Advanced desligados não podem consumir trabalho GPU ou VRAM exclusivo;
- VRAM equivalente <= OptiScaler+NRFusion atual, salvo tradeoff medido e aprovado;
- medir p50/p95/p99, não apenas média;
- otimização que melhora média e piora p99 é regressão até análise;
- não afirmar ganho real de FPS/GPU sem hardware real;
- compatibilidade excepcional deve ser gated e não taxar todos os jogos.

## Estratégia de validação

Três níveis, nesta ordem:

1. **CPU/fake path** — `nrfusion_sim`, tests existentes e FakeNrExecutor quando necessário.
2. **Harness gráfico** — evoluir os harnesses/testes existentes para cenários determinísticos sem jogo real.
3. **Jogos reais** — driver, imagem, VRAM e performance final.

O harness deve permitir executor fake quando possível para que captura/policy/lifecycle continuem testáveis sem GPU NVIDIA/DLSS.

Antes de usar jogo real para diagnosticar um bug, tentar reproduzi-lo no menor harness existente.

Harnesses não são produtos:

- sem engine, assets, física ou UI elaborada;
- sem abstraction layer gráfica genérica;
- frontends finos por API;
- cenários determinísticos por CLI;
- saída estruturada de métricas;
- menos código é melhor.

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
| 20 | Hot-path audit |
| 21 | Executor optimization |
| 22 | Qualification |
| 23 | Cutover |

## Regras de execução

- O arquivo da fase atual é a fonte de verdade.
- Não iniciar fase dependente antes do gate anterior, salvo blocker explicitamente documentado.
- Cada item deve ser pequeno e verificável isoladamente.
- Estabilizar lote antes de mover branch/rodar CI completo.
- CI confirma integração; não substitui revisão.
- Mudança de hot path sempre registra: antes, depois, trabalho removido/adicionado e comportamento preservado.
- Barrier, ownership, lock e fallback exigem justificativa concreta.

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

NRFusion só substitui OptiScaler quando NR funcionar por rotas qualificadas independentemente de DLSS nativo no jogo, com menor overhead de host para trabalho equivalente e sem regressão de VRAM equivalente.
