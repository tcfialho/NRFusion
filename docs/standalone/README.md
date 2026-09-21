# NRFusion Universal Standalone

## Missão

Transformar o NRFusion em um host universal de DLSS 5 sem depender do OptiScaler e sem exigir DLSS nativo no jogo.

Escopo-alvo:

- x64 e x86;
- D3D12, D3D11, D3D10, D3D9, Vulkan e OpenGL;
- providers Native, Bridge e Synthetic;
- Neural Rendering universal sempre que existir uma rota tecnicamente válida;
- MFG onde Streamline/DLSSG/presentation puderem ser integrados corretamente;
- executor DLSS 5 D3D12 x64 canônico sempre que tecnicamente possível.

## Arquitetura

A API do jogo determina **como** o frame chega ao NRFusion, não **se** Neural Rendering existe.

```text
Tem contrato DLSS/RR utilizável?
  sim -> Native/Bridge
  não -> Synthetic

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

## Contrato rígido de performance

Em steady state normal:

```text
heap allocations/frame          = 0
GPU resource creation/frame     = 0
descriptor heap creation/frame  = 0
filesystem access/frame         = 0
string/config parsing/frame     = 0
shader compilation/frame        = 0
blocking GPU wait/frame         = 0
```

Também é obrigatório:

- evitar mutex no caminho normal; lock exige concorrência real demonstrada;
- Diagnostics/Advanced desligados não podem consumir trabalho GPU nem VRAM exclusivo;
- VRAM equivalente deve ser <= OptiScaler+NRFusion atual, salvo tradeoff medido e aprovado;
- medir p50/p95/p99, não só média;
- não afirmar ganho real de FPS/GPU sem hardware real;
- mover código GPU maduro antes de otimizá-lo;
- compatibilidade excepcional não pode taxar todos os jogos.

## Estratégia de validação

O desenvolvimento usa três níveis:

1. **FakeNrExecutor**: milhões de frames de policy/state sem GPU.
2. **MiniGame Harness**: plumbing gráfico mínimo e determinístico sem jogo real.
3. **Jogos reais**: qualificação final de driver, imagem, VRAM e performance real.

Antes de recorrer a jogo real para diagnosticar um problema, tentar reproduzi-lo no harness mínimo.

Harnesses não são produtos:

- sem engine;
- sem assets;
- sem física/câmera/UI elaborada;
- sem framework gráfico genérico desnecessário;
- preferir poucas centenas de linhas úteis por frontend;
- cenários determinísticos por argumentos;
- saída estruturada de métricas.

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
| 11 | Vulkan carrier |
| 12 | OpenGL carrier |
| 13 | D3D10 carrier |
| 14 | D3D9 carrier |
| 15 | Guide acquisition |
| 16 | MFG standalone |
| 17 | Menu/config |
| 18 | Compatibility |
| 19 | VRAM/resources |
| 20 | Hot-path audit |
| 21 | Executor optimization |
| 22 | Qualification |
| 23 | Cutover |

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
- [11 — Vulkan carrier](11-vulkan-carrier.md)
- [12 — OpenGL carrier](12-opengl-carrier.md)
- [13 — D3D10 carrier](13-d3d10-carrier.md)
- [14 — D3D9 carrier](14-d3d9-carrier.md)
- [15 — Guide acquisition](15-guide-acquisition.md)
- [16 — MFG](16-mfg.md)
- [17 — Menu/config](17-menu.md)
- [18 — Compatibility](18-compatibility.md)
- [19 — VRAM/resources](19-vram-resources.md)
- [20 — Hot-path audit](20-hotpath-audit.md)
- [21 — Executor optimization](21-executor-optimization.md)
- [22 — Qualification](22-qualification.md)
- [23 — Cutover](23-cutover.md)

## Regras globais de revisão

Toda mudança de hot path deve responder:

1. o que executava antes;
2. o que executa depois;
3. qual trabalho foi removido/adicionado;
4. qual comportamento observável foi preservado.

Além disso:

- nenhuma abstração nova sem custo de dispatch, ownership, sync, memória e lifetime conhecido;
- nenhum mutex sem threads concorrentes identificadas;
- nenhuma mudança de barrier sem estados anterior/próximo provados;
- COM/native ownership explícito;
- error paths recebem o mesmo nível de revisão do happy path;
- fallback nunca esconde provenance;
- CI valida integração, não substitui revisão;
- estabilizar lote antes de atualizar branch/rodar build completo.

## Critério final

O cutover só acontece quando o NRFusion puder fornecer DLSS 5 Neural Rendering independentemente de DLSS nativo no jogo, escolhendo uma rota Native/Bridge/Synthetic qualificada para API e bitness, com menor overhead de host que a arquitetura OptiScaler equivalente e sem regressão de VRAM equivalente.
