# Fase 17 — Menu e configuração

## Objetivo

Manter o menu normal mínimo e deslocar override/diagnóstico para Advanced sem custo oculto.

## Dependências

Fases 02,06,16.

## Fora de escopo

- Replicar UI do OptiScaler
- Construir widgets ocultos todo frame

## Implementação

- [ ] Menu principal: Enabled.
- [ ] Neural Rendering Mode: Auto / Best quality / Performance / Custom.
- [ ] Target FPS com ação Display Hz.
- [ ] MFG Mode: Game Controlled / 2X / 3X / 4X / Dynamic.
- [ ] MFG Quality: Performance / Enhanced.
- [ ] Status: NR Scale, NR Cost, MFG, Execution, State.
- [ ] Advanced collapsed por padrão com precision, DLSS5 appearance, HDR/exposure, placement/residual, multipass, MFG experimental e diagnostics úteis.
- [ ] Config textual é convertida em RuntimeConfig somente em load/change.

## Revisão obrigatória

- [ ] Advanced item só existe se trouxer controle/diagnóstico real.
- [ ] Hidden/inactive não aloca VRAM nem adiciona GPU work.
- [ ] Status usa snapshot sem travar render thread.
- [ ] Abrir/fechar UI não altera policy ou feature lifetime.

## Validação rápida

- [ ] Harness mede menu fechado/aberto sem alterar cenário.
- [ ] Persist/reload e invalid config.
- [ ] Confirmar 0 parsing/frame.

## Gate

- [ ] Menu principal continua simples.
- [ ] Advanced preserva poder sem taxar default.
- [ ] UI não vira dependência do runtime.

## Próxima fase

Fase 18.
