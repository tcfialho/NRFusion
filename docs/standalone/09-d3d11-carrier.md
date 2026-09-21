# Fase 09 — D3D11 x64 bridge/carrier

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose para D3D11 x64.

## Dependências

Fases 07–08.

## Fora de escopo

- x86
- CPU frame transport

## Implementação

- [ ] Partir de `SyntheticDx11BridgeProvider` e teste existente.
- [ ] Separar o hook/acquisition D3D11 do bridge D3D11→D3D12.
- [ ] Definir slot/fence ownership e reuse.
- [ ] Capturar color; depth/motion apenas com provenance válida.
- [ ] Executar no D3D12 canônico e compor de volta.
- [ ] Tratar resize/device/context destruction.
- [ ] Evoluir o teste existente para long-run/in-flight.

## Revisão obrigatória

- [ ] Bridge funcional não prova Acquire universal.
- [ ] Keyed mutex/fence ordering.
- [ ] Allocator/slot só após retirement.
- [ ] Cada full-frame CopyResource é identificado e justificado.
- [ ] Lock atual do provider entra na auditoria, não é automaticamente preservado.

## Validação rápida

- [ ] Teste atual continua passando.
- [ ] Hook/acquisition recebe cenário controlado no harness.
- [ ] Stress in-flight/resize/reset.
- [ ] Medir copies, waits e CPU.

## Gate

- [ ] Acquire e bridge são ambos demonstrados.
- [ ] Transporte GPU-resident.
- [ ] Steady-state sem resource creation/alocação do carrier.

## Próxima fase

Fase 10.
