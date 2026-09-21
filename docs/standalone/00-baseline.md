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
- [ ] Registrar rotas existentes x86/Host64, D3D11, Vulkan e OpenGL.
- [ ] Registrar menu principal/Advanced que precisam sobreviver.
- [ ] Inventariar todos os first-party handwritten files >300 linhas.
- [ ] Classificar cada oversized file: **split na fase dona**, **aposentar**, ou **cleanup antes do cutover**.
- [ ] Marcar infraestrutura OptiScaler sem uso direto pelo NRFusion.

## Revisão obrigatória

- [ ] Toda responsabilidade necessária recebe novo owner.
- [ ] Workaround sem causa comprovada é preservado.
- [ ] Arquivo grande não é dividido arbitrariamente: boundary precisa seguir responsabilidade/lifetime.
- [ ] Arquivo legado >300 não recebe crescimento durante transição.
- [ ] Limites dependentes de hardware real ficam explícitos.

## Validação rápida

- [ ] Gerar call-path estático D3D12.
- [ ] Contar adapter calls, locks, timers e config accesses relevantes.
- [ ] Gerar relatório simples de arquivos >300 por path/categoria.
- [ ] Conferir CMake/installer/build_dist.

## Gate

- [ ] Nenhuma responsabilidade crítica sem owner.
- [ ] Todo oversized first-party file tem destino explícito.
- [ ] Baseline permite comparação diferencial posterior.

## Próxima fase

Fase 01.
