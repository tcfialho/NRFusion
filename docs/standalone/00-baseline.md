# Fase 00 — Baseline e mapa de responsabilidades

## Objetivo

Congelar responsabilidades e comportamento antes de mover código.

## Dependências

Nenhuma.

## Fora de escopo

- Alterar comportamento
- Otimizar executor
- Criar carrier novo

## Implementação

- [ ] Registrar master/upstreams e artefatos atuais.
- [ ] Mapear cada patch point de apply_to_optiscaler.py para Host/Provider/Executor/MFG/Menu/Diagnostics/Compatibility.
- [ ] Inventariar hooks, recursos GPU, readbacks, query heaps e lifetimes.
- [ ] Registrar create/rebuild/reset/history/fallback atuais.
- [ ] Registrar rotas existentes x86/Host64, D3D11, Vulkan e OpenGL.
- [ ] Registrar menu principal e Advanced que precisam sobreviver.
- [ ] Marcar infraestrutura OptiScaler sem uso direto pelo NRFusion.

## Revisão obrigatória

- [ ] Toda responsabilidade necessária recebe novo owner.
- [ ] Workaround sem causa comprovada é preservado até investigação.
- [ ] Separar requisito funcional de conveniência do host atual.
- [ ] Registrar limites que ainda dependem de hardware real.

## Validação rápida

- [ ] Gerar call-path estático do frame D3D12 atual.
- [ ] Contar chamadas de adapter, locks, timers e config accesses relevantes.
- [ ] Conferir CMake, installer e build_dist contra o inventário.

## Gate

- [ ] Nenhuma responsabilidade crítica fica sem owner.
- [ ] Baseline é suficiente para comparar a extração sem depender de memória informal.

## Próxima fase

Fase 01.
