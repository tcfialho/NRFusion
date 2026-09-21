# Fase 07 — D3D12 x64 carrier

## Objetivo

Completar a primeira rota standalone usando o contrato universal e executor canônico.

## Dependências

Fases 04–06.

## Fora de escopo

- Declarar outras APIs suportadas
- Universalizar MFG

## Implementação

- [ ] Adquirir device/queue e seam de frame correto.
- [ ] Construir FrameContract sem copiar ownership do jogo.
- [ ] Integrar feature registry e NrSession.
- [ ] Compor resultado preservando caller state.
- [ ] Tratar resize, device removal e guides ausentes.
- [ ] Disabled path quase pass-through.
- [ ] Manter pixels GPU-resident.

## Revisão obrigatória

- [ ] Sem CPU copy/readback no caminho normal.
- [ ] Nenhum GPU resource creation steady-state.
- [ ] Nenhum blocking wait/frame.
- [ ] Resource states e lifetime do jogo preservados.
- [ ] Seam escolhido não pode executar NR duas vezes.

## Validação rápida

- [ ] MiniGame D3D12 com fake executor: steady/resize/reset/HDR/missing guides.
- [ ] Long run p50/p95/p99 do host.
- [ ] Com hardware compatível, trocar para --executor=dlss sem mudar cenário.

## Gate

- [ ] Rota estrutural validada sem jogo real.
- [ ] Fast path cumpre contrato.
- [ ] Modelo real fica como gate de hardware, não blocker do desenvolvimento.

## Próxima fase

Fase 08.
