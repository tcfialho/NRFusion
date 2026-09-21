# Fase 11 — Vulkan carrier

## Objetivo

Qualificar Vulkan→D3D12 com external memory/semaphore corretos.

## Dependências

Fases 07–08.

## Fora de escopo

- Compile-only claim
- Constantes Vulkan inventadas

## Implementação

- [ ] Trocar structs/constants ad-hoc por headers/contratos verificados quando necessário.
- [ ] Definir external-memory compatibility e memoryTypeIndex corretamente.
- [ ] Definir timeline/binary semaphore strategy e ownership.
- [ ] Definir layouts, access masks e queue-family transfer.
- [ ] Capturar color e guides disponíveis com provenance.
- [ ] Compor de volta e tratar swapchain/device recreation.
- [ ] Adicionar frontend Vulkan fino ao runner.

## Revisão obrigatória

- [ ] Handle import/export ownership.
- [ ] Image format/tiling/usage realmente compatíveis com interop.
- [ ] Queue/layout transitions completos em success/failure.
- [ ] Sem CPU readback.

## Validação rápida

- [ ] Harness Vulkan com fake executor quando runtime disponível.
- [ ] Resize/recreate e interop failure.
- [ ] Long run de semaphore values.

## Gate

- [ ] Rota GPU-resident demonstrada.
- [ ] Nenhuma assumption Vulkan sem fonte técnica/código verificável.
- [ ] Failure desativa rota sem corromper o jogo.

## Próxima fase

Fase 12.
