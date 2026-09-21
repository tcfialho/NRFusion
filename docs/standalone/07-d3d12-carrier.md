# Fase 07 — D3D12 x64 carrier

## Objetivo

Completar a primeira rota standalone usando o contrato universal e o executor canônico.

## Dependências

Fases 04–06.

## Fora de escopo

- Declarar outras APIs suportadas
- Universalizar MFG

## Implementação

- [ ] Reusar `SyntheticDx12Provider`, `nrfusion_synthetic_dx12_test` e harness 3D como ponto de partida, não criar rota paralela.
- [ ] Adquirir device/queue e seam correto do jogo.
- [ ] Construir FrameContract sem transferir ownership dos resources do game.
- [ ] Integrar feature registry e NrSession.
- [ ] Compor resultado preservando caller state.
- [ ] Tratar resize, device removal e guides ausentes.
- [ ] Disabled path quase pass-through.
- [ ] Manter pixels GPU-resident.

## Revisão obrigatória

- [ ] Distinguir custo necessário do carrier do custo hoje existente em SyntheticDx12Provider.
- [ ] Nenhum CPU copy/readback no normal path.
- [ ] `EnsureSlotResources` só pode criar em init/reconfigure/resolution change, nunca steady-state.
- [ ] Locks e descriptor writes por Submit/Compose são contados para a Fase 20.
- [ ] Nenhum blocking wait/frame.
- [ ] Seam escolhido não executa NR duas vezes.

## Validação rápida

- [ ] Estender os testes D3D12 existentes antes de criar novo executable.
- [ ] Harness: steady/resize/reset/HDR/missing guides.
- [ ] Long run p50/p95/p99 do host.
- [ ] Modelo real apenas quando hardware compatível existir.

## Gate

- [ ] Rota estrutural validada sem jogo real.
- [ ] Fast path cumpre o contrato de steady-state.
- [ ] Necessidade de hardware real está isolada ao executor/qualification.

## Próxima fase

Fase 08.
