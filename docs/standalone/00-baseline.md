# Fase 00 — Baseline e mapa de responsabilidades

## Status

**Concluída.** Evidência: [00-baseline-evidence.md](00-baseline-evidence.md).

## Objetivo

Congelar responsabilidades/comportamento e mapear dívida estrutural antes de mover código.

## Dependências

Nenhuma.

## Fora de escopo

- Alterar comportamento
- Otimizar executor
- Refatorar em massa só para reduzir LOC

## Implementação

- [x] Registrar master/upstreams e artefatos atuais.
- [x] Mapear patch points do OptiScaler por Host/Provider/Executor/MFG/Menu/Diagnostics/Compatibility.
- [x] Inventariar hooks, recursos GPU, readbacks, query timing e lifetimes relevantes.
- [x] Registrar create/rebuild/reset/history/fallback atuais.
- [x] Registrar rotas x86/Host64, D3D11, Vulkan e OpenGL existentes.
- [x] Inventariar todos os first-party handwritten files >300 linhas.
- [x] Classificar cada oversized file por fase dona ou retirement.
- [x] Criar checker mínimo de linhas físicas.
- [x] Validar modos `changed`, `all` e `all --strict`.
- [x] Marcar infraestrutura OptiScaler que será aposentada.

## Revisão obrigatória

- [x] Toda responsabilidade necessária tem novo owner/fase.
- [x] Workaround sem causa comprovada permanece preservado.
- [x] Splits previstos seguem responsabilidade/lifetime.
- [x] Legado >300 fica read-only/no-growth até a fase dona.
- [x] Checker não usa allowlist ad-hoc.
- [x] Limites dependentes de hardware real estão explícitos.

## Validação rápida

- [x] Call-path D3D12 atual registrado.
- [x] Adapter calls/locks/timing/config baseline registrados.
- [x] Inventário completo de source-size produzido pela API do GitHub.
- [x] Checker validado em repositório Git temporário com regressões de vendor/untracked.
- [x] CMake/installer/build_dist conferidos.

O ambiente desta sessão não possui checkout executável do repositório; por isso `check_source_size.py all`
não foi executado contra o repo real. O inventário completo foi produzido independentemente pela API,
e o checker foi testado funcionalmente em um repositório Git temporário.

## Gate

- [x] Nenhuma responsabilidade crítica sem owner.
- [x] Todo oversized first-party file tem destino.
- [x] Checker `changed/all` disponível para as fases seguintes.
- [x] Baseline permite comparação diferencial.

## Próxima fase

Fase 01 — Universal FrameContract.
