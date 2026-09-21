# Fase 09 — D3D11 x64 bridge/carrier

## Objetivo

Qualificar SyntheticDx11BridgeProvider como rota real.

## Dependências

Fases 07–08.

## Fora de escopo

- x86
- CPU frame transport

## Checklist de implementação

- [ ] Revisar shared D3D12 path.
- [ ] Remover waits evitáveis.
- [ ] Validar slots/fences/formats.
- [ ] Color GPU-side.
- [ ] Depth/motion quando possível.
- [ ] Fallback motion explícito.
- [ ] Executor canônico.
- [ ] Compose D3D11.
- [ ] Resize/device/context destruction.
- [ ] Frontend D3D11 mínimo.

## Revisão obrigatória

- [ ] Keyed mutex/fence.
- [ ] Allocator só após retirement.
- [ ] N/N+1 aliasing.
- [ ] Evitar copies full-frame redundantes.
- [ ] Guide provenance.

## Validação rápida

- [ ] D3D11 harness steady/resize/reset.
- [ ] Long run in-flight.
- [ ] Medir CPU/copies.

## Gate de conclusão

- [ ] GPU-only route qualificada.
- [ ] Sem waits injustificados.
- [ ] 0 alloc/resource create steady.

## Entregáveis

- D3D11 carrier
- D3D11 harness

## Próxima fase

Fase 10.
