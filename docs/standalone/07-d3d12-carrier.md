# Fase 07 — D3D12 x64 carrier

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose para D3D12 x64.

## Dependências

Fases 04–06.

## Fora de escopo

- Declarar outras APIs suportadas
- Universalizar MFG

## Implementação

- [ ] Reusar `SyntheticDx12Provider`, testes D3D12 e harness 3D para Execute/Compose.
- [ ] Definir separadamente o Acquire seam do jogo: onde color/depth/motion/exposure são observados e com qual lifetime.
- [ ] Para jogo com DLSS/RR, aproveitar contrato existente quando confiável.
- [ ] Para jogo sem DLSS, construir Synthetic FrameContract apenas com recursos realmente adquiridos.
- [ ] Integrar feature registry e NrSession.
- [ ] Compor resultado preservando caller state.
- [ ] Tratar resize/device removal/guides ausentes.
- [ ] Disabled path quase pass-through.

## Revisão obrigatória

- [ ] SyntheticDx12Provider aceitar ResourceRef não conta como prova de Acquire.
- [ ] Nenhum CPU pixel path.
- [ ] `EnsureSlotResources` cria apenas em init/reconfigure/resolution change.
- [ ] Locks, descriptor writes e copies entram na Fase 20.
- [ ] Seam não executa NR duas vezes nem usa recurso pós-lifetime.

## Validação rápida

- [ ] Testes existentes cobrem Execute/Compose.
- [ ] Harness exercita Acquire sintético/controlado e lifecycle.
- [ ] Jogos reais ficam para provar Acquire em engines reais e modelo DLSS.

## Gate

- [ ] As quatro partes possuem owner e caminho verificável.
- [ ] Fast path cumpre contrato steady-state.
- [ ] Limites que só hardware/game real pode provar estão isolados.

## Próxima fase

Fase 08.
