# Fase 09 — D3D11 x64 bridge/carrier

## Objetivo

Qualificar a rota D3D11→D3D12 preservando pixels na GPU.

## Dependências

Fases 07–08.

## Fora de escopo

- x86
- CPU frame transport

## Implementação

- [ ] Partir de `SyntheticDx11BridgeProvider` e `nrfusion_synthetic_dx11_bridge_test`.
- [ ] Definir slot/fence ownership e reuse sem reescrever o que já funciona.
- [ ] Capturar color; depth/motion apenas com provenance válida.
- [ ] Abrir/usar resources no executor D3D12 canônico.
- [ ] Compor de volta no D3D11.
- [ ] Tratar resize/device/context destruction.
- [ ] Evoluir o teste existente para cenários longos/in-flight em vez de criar novo harness.

## Revisão obrigatória

- [ ] Keyed mutex/fence ordering.
- [ ] Allocator/slot só reutilizado após retirement.
- [ ] Sem frame N/N+1 aliasing.
- [ ] Cada full-frame CopyResource é identificado e justificado.
- [ ] O lock atual do provider entra na auditoria de hot path, não é automaticamente preservado.

## Validação rápida

- [ ] Teste atual continua passando.
- [ ] Adicionar steady/resize/reset/in-flight ao mesmo executable quando simples.
- [ ] Medir copies, waits e host CPU.

## Gate

- [ ] Transporte GPU-resident demonstrado.
- [ ] Sem wait/copy injustificado.
- [ ] Steady-state sem resource creation/alocação do carrier.

## Próxima fase

Fase 10.
