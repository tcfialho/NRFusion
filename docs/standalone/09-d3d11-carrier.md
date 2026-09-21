# Fase 09 — D3D11 x64 bridge/carrier

## Objetivo

Qualificar a rota D3D11→D3D12 preservando pixels na GPU.

## Dependências

Fases 07–08.

## Fora de escopo

- x86
- CPU frame transport

## Implementação

- [ ] Auditar shared-resource path existente antes de reescrever.
- [ ] Definir slot/fence ownership e reuse.
- [ ] Capturar color; depth/motion apenas com provenance válida.
- [ ] Abrir/usar recursos no D3D12 canônico.
- [ ] Compor resultado de volta no D3D11.
- [ ] Tratar resize/device/context destruction.
- [ ] Adicionar frontend D3D11 mínimo ao mesmo runner.

## Revisão obrigatória

- [ ] Keyed mutex/fence ordering.
- [ ] Allocator/slot só reutilizado após retirement.
- [ ] Evitar N/N+1 aliasing.
- [ ] Cada full-frame copy deve ser inevitável e contada.
- [ ] Guide fallback é policy, não truque do carrier.

## Validação rápida

- [ ] Harness D3D11 com fake executor.
- [ ] Stress in-flight/resize/reset.
- [ ] Medir copies e host CPU.

## Gate

- [ ] GPU-resident transport demonstrado.
- [ ] Sem wait/copy injustificado.
- [ ] Steady-state sem criação/alocação do carrier.

## Próxima fase

Fase 10.
