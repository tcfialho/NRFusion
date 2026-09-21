# Fase 17 — Menu e configuração

## Objetivo

Menu principal pequeno; Advanced preserva poder sem custo inativo.

## Dependências

Fases 02,06,16.

## Fora de escopo

- Replicar OptiScaler UI
- Widgets invisíveis por frame

## Checklist de implementação

- [ ] Principal: Enabled.
- [ ] NR Mode + Target FPS/Display Hz.
- [ ] MFG Mode + Quality.
- [ ] Status: Scale/Cost/MFG/Execution/State.
- [ ] Advanced collapsed.
- [ ] Advanced: precision, DLSS5 appearance, HDR/exposure, residual/placement, multipass, MFG experimental, diagnostics.
- [ ] Config strings convertidas uma vez.
- [ ] Menu fechado evita layout/widget desnecessário.

## Revisão obrigatória

- [ ] Cada Advanced: útil? Auto cobre? aloca VRAM? custa steady-state?.
- [ ] Inativo não mantém resource exclusivo.
- [ ] Abrir menu não muda policy.
- [ ] Status sem travar render.

## Validação rápida

- [ ] Harness abre/fecha menu em benchmark.
- [ ] Comparar allocations/CPU fechado.
- [ ] Persist/reload.

## Gate de conclusão

- [ ] Principal simples.
- [ ] Advanced sem custo inativo.
- [ ] Sem parsing/frame.

## Entregáveis

- Menu contract
- Advanced inventory

## Próxima fase

Fase 18.
