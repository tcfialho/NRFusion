# Fase 07 — D3D12 x64 carrier

## Objetivo

Completar primeira rota standalone real.

## Dependências

Fases 04–06.

## Fora de escopo

- Declarar outras APIs supported
- MFG universal

## Checklist de implementação

- [ ] Adquirir device/queue/resources.
- [ ] Construir FrameContract.
- [ ] Integrar registry.
- [ ] Executar NrSession/Executor.
- [ ] Compor resultado.
- [ ] Resize/device removal.
- [ ] Ausência de guides.
- [ ] Disabled quase pass-through.
- [ ] Resources GPU-resident.

## Revisão obrigatória

- [ ] Ownership do game resource.
- [ ] Sem CPU copy.
- [ ] Sem resource create/frame.
- [ ] Sem blocking wait/frame.
- [ ] Estados D3D12 preservados.

## Validação rápida

- [ ] MiniGame steady/resize/reset/missing guides/HDR.
- [ ] Long run p50/p95/p99.
- [ ] Confirmar 0 allocations/resource creates pós-warm-up.

## Gate de conclusão

- [ ] D3D12 qualificado no harness.
- [ ] Fast path respeita contrato.

## Entregáveis

- D3D12 carrier
- Harness report

## Próxima fase

Fase 08.
