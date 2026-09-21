# Fase 23 — A/B final e cutover do OptiScaler

## Objetivo

Remover OptiScaler do caminho principal só após equivalência e performance.

## Dependências

Fase 22.

## Fora de escopo

- Apagar fallback cedo
- Claim GPU sem hardware

## Checklist de implementação

- [ ] CPU ns/frame p50/p95/p99.
- [ ] Allocations/bytes/locks/resource creates.
- [ ] Timestamp/query counts.
- [ ] VRAM equivalente.
- [ ] Real GPU: frame ms/NR ms/FPS/1% low/p95/p99/MFG pacing.
- [ ] Menu/config/fallback/recovery.
- [ ] Status das rotas não qualificadas.
- [ ] Manter OptiScaler fallback/referência na transição.
- [ ] Rollback claro.

## Revisão obrigatória

- [ ] A/B equivalente.
- [ ] Warm-up/carga comparáveis.
- [ ] Resultados negativos visíveis.
- [ ] Não depender de um único jogo.

## Validação rápida

- [ ] Harness completo antes de jogos.
- [ ] Real hardware nas rotas prioritárias.
- [ ] Stress resize/reset/MFG.

## Gate de conclusão

- [ ] D3D12 x64 qualificado.
- [ ] FrameContract/registry estáveis.
- [ ] x86 Host64 qualificado.
- [ ] D3D11/Vulkan/OpenGL conforme release target ou Blocked explícito.
- [ ] D3D9/D3D10 status explícito.
- [ ] MFG preservado onde suportado.
- [ ] 0 heap/resource create steady.
- [ ] VRAM sem regressão equivalente.
- [ ] CPU abaixo do baseline.
- [ ] Real hardware sem regressão material.

## Entregáveis

- Cutover report
- Final matrix
- Rollback plan

## Próxima fase

Fim.
