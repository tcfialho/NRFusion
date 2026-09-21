# Fase 13 — D3D10 carrier

## Objetivo

Bridge D3D10 para executor canônico.

## Dependências

Fases 07–08.

## Fora de escopo

- Executor NR D3D10
- CPU screenshot

## Checklist de implementação

- [ ] Identificar DXGI sharing.
- [ ] Device/adapter identity.
- [ ] Color GPU-side.
- [ ] Depth disponível/provenance.
- [ ] Motion strategy.
- [ ] Bridge D3D12.
- [ ] Sync sem blocking quando possível.
- [ ] Compose back.
- [ ] Resize/recreation.
- [ ] Frontend D3D10.

## Revisão obrigatória

- [ ] Cada full-frame copy justificada.
- [ ] Fail closed sem sharing.
- [ ] Não mascarar guides ausentes.
- [ ] Shared lifetime.

## Validação rápida

- [ ] D3D10 steady/resize.
- [ ] Com/sem guides.
- [ ] Medir copies/CPU.

## Gate de conclusão

- [ ] Demonstrada ou explicitamente Blocked.
- [ ] Sem CPU pixel transport.

## Entregáveis

- D3D10 carrier
- D3D10 harness

## Próxima fase

Fase 14.
