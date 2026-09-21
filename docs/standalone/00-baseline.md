# Fase 00 — Baseline e mapa de responsabilidades

## Objetivo

Congelar responsabilidades/comportamento e mapear dívida estrutural antes de mover código.

## Dependências

Nenhuma.

## Fora de escopo

- Alterar comportamento
- Otimizar executor
- Refatorar em massa só para reduzir LOC

## Implementação

- [ ] Registrar master/upstreams e artefatos atuais.
- [ ] Mapear patch points do OptiScaler por Host/Provider/Executor/MFG/Menu/Diagnostics/Compatibility.
- [ ] Inventariar hooks, recursos GPU, readbacks, query heaps e lifetimes.
- [ ] Registrar create/rebuild/reset/history/fallback atuais.
- [ ] Registrar rotas x86/Host64, D3D11, Vulkan e OpenGL existentes.
- [ ] Inventariar **todos** os first-party handwritten files >300 linhas.
- [ ] Classificar cada um: **split na fase dona**, **aposentar**, ou **cleanup antes do cutover**.
- [ ] Marcar infraestrutura OptiScaler sem uso direto pelo NRFusion.

## Passivo conhecido no master `410cab8`

| Arquivo | Linhas | Destino inicial |
|---|---:|---|
| `tools/apply_to_optiscaler.py` | 3900 | aposentar no cutover |
| `tests/controller_tests.cpp` | 2743 | split antes da RC |
| `src/CaptureD3D11.cpp` | 1302 | Fase 09/10 |
| `src/AdaW4A8Interceptor.cpp` | 1097 | Fase 05/21 |
| `tests/harness_3d/D3D12TestHarness.cpp` | 1085 | Fase 03 |
| `src/cuda/W4A8FfnSm89.cu` | 946 | Fase 05/21 |
| `src/OptiScalerAdapter.cpp` | 843 | aposentar com NrSession |
| `src/HostServer64.cpp` | 659 | Fase 10 |
| `src/ProfileStore.cpp` | 666 | split se mantido |
| `src/SyntheticDx12Provider.cpp` | 596 | Fase 07 |
| `src/DlssgTransfusion.cpp` | 555 | Fase 16 |
| `src/PerformanceController.cpp` | 514 | Fase 06/20 |
| `src/SyntheticOpenGlProvider.cpp` | 409 | Fase 12 |
| `src/SyntheticVulkanProvider.cpp` | 377 | Fase 11 |
| `src/CaptureProvider32.cpp` | 370 | Fase 10 |
| `src/CompatibilityDatabase.cpp` | 350 | Fase 18 |
| `include/nrfusion/FusionRuntime.hpp` | 329 | Fase 06 |
| `tools/build_dist.ps1` | 395 | Fase 23 |

A tabela é amostra inicial; o inventário completo da fase é autoritativo.

## Revisão obrigatória

- [ ] Toda responsabilidade necessária recebe novo owner.
- [ ] Workaround sem causa comprovada é preservado.
- [ ] Split segue responsabilidade/lifetime, nunca tamanho arbitrário.
- [ ] Arquivo legado >300 não cresce.
- [ ] Limites dependentes de hardware real ficam explícitos.

## Validação rápida

- [ ] Gerar call-path D3D12.
- [ ] Contar adapter calls, locks, timers e config accesses.
- [ ] Gerar relatório completo de arquivos >300.
- [ ] Conferir CMake/installer/build_dist.

## Gate

- [ ] Nenhuma responsabilidade crítica sem owner.
- [ ] Todo oversized first-party file tem destino.
- [ ] Baseline permite comparação diferencial.

## Próxima fase

Fase 01.
