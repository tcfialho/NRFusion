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
- [ ] NR Mode: Auto / Best quality / Performance / Custom.
- [ ] Target FPS preserva a semântica atual de alvo de FPS renderizado; `Auto (Display Hz)` mantém o comportamento atual de usar o refresh detectado.
- [ ] Não reinterpretar Target FPS automaticamente pelo multiplicador MFG sem uma decisão de produto separada.
- [ ] MFG Mode: Game Controlled / 2X / 3X / 4X / Dynamic; Quality: Performance / Enhanced.
- [ ] Quando MFG não estiver qualificado, mostrar indisponibilidade/status em vez de esconder problema ou afetar NR.
- [ ] Status: NR Scale, NR Cost, MFG, Execution, State.
- [ ] Advanced collapsed: precision, DLSS5 appearance, HDR/exposure, placement/residual, multipass, MFG experimental e diagnostics úteis.
- [ ] Config textual vira RuntimeConfig somente em load/change; refresh rate é consultado apenas quando necessário à UI/config.

## Revisão obrigatória

- [ ] Advanced item só existe se trouxer controle/diagnóstico real.
- [ ] Hidden/inactive não aloca VRAM nem adiciona GPU work.
- [ ] Status usa snapshot sem travar render thread.
- [ ] Abrir/fechar UI não muda policy ou feature lifetime.
- [ ] Labels não confundem rendered FPS com generated/displayed FPS.

## Validação rápida

- [ ] Harness mede menu fechado/aberto sem alterar cenário.
- [ ] Persist/reload e config inválida.
- [ ] Confirmar 0 parsing/frame e nenhum refresh query desnecessário no hot path.

## Gate

- [ ] Menu principal continua simples.
- [ ] Advanced preserva poder sem taxar default.
- [ ] Semântica Target FPS/MFG está explícita e estável.

## Próxima fase

Fase 18.
