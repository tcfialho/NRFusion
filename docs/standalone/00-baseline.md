# Fase 00 — Baseline e mapa de responsabilidades

## Objetivo

Congelar o comportamento atual e separar NRFusion de infraestrutura OptiScaler.

## Dependências

Nenhuma.

## Fora de escopo

- Alterar comportamento
- Otimizar executor
- Criar novos carriers

## Checklist de implementação

- [ ] Registrar SHA/master e upstreams.
- [ ] Inventariar patch points de apply_to_optiscaler.py.
- [ ] Classificar Host/Provider/Executor/MFG/Menu/Diagnostics/Compatibility.
- [ ] Inventariar recursos GPU, readbacks e query heaps.
- [ ] Inventariar hooks de NR/MFG e hooks genéricos dispensáveis.
- [ ] Registrar create/rebuild/reset/history/fallback atuais.
- [ ] Registrar x86/Host64, D3D11, Vulkan e OpenGL existentes.
- [ ] Registrar menu principal e Advanced útil.

## Revisão obrigatória

- [ ] Definir novo owner para cada responsabilidade.
- [ ] Distinguir requisito real de infraestrutura genérica.
- [ ] Marcar semântica incerta para não reescrever por suposição.

## Validação rápida

- [ ] Gerar mapa de chamadas do frame path.
- [ ] Contar locks/timers/config reads do D3D12 atual.
- [ ] Conferir CMake/installer/build_dist.

## Gate de conclusão

- [ ] Nenhuma responsabilidade necessária sem owner.
- [ ] Baseline permite comparação diferencial posterior.

## Entregáveis

- Mapa de responsabilidades
- Inventário de hooks/resources
- Baseline de hot path

## Próxima fase

Fase 01.
