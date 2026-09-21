# Fase 11 — Vulkan carrier

## Objetivo

Transformar o suporte Vulkan atual de estrutural/simulado em interop real qualificado.

## Dependências

Fases 07–08.

## Fora de escopo

- Tratar o teste com fake handles como prova de interop real
- Constantes/structs Vulkan não verificadas

## Implementação

- [ ] Reusar `SyntheticVulkanProvider` e separar claramente code path simulado de path com VkDevice real.
- [ ] Usar headers/contratos Vulkan corretos para structs/constants.
- [ ] Selecionar memoryTypeIndex a partir de requirements/properties reais.
- [ ] Definir external-memory compatibility, semaphore type e handle ownership.
- [ ] Definir layouts, access masks e queue-family ownership.
- [ ] Capturar color/guides com provenance e compor de volta.
- [ ] Tratar swapchain/device recreation.
- [ ] Criar/estender harness Vulkan somente para o gap que o `ipc_host_test` atual não cobre.

## Revisão obrigatória

- [ ] Fake handle test é apenas contract test.
- [ ] Image format/tiling/usage realmente compatíveis com D3D12 external memory.
- [ ] Sync e ownership completos em success/failure.
- [ ] Sem CPU readback.

## Validação rápida

- [ ] Contract tests continuam úteis sem Vulkan real.
- [ ] Interop real é exercitado quando VkDevice/extensões existirem.
- [ ] Resize/recreate e semaphore long run.

## Gate

- [ ] Harness-verified só após VkImage/memory/semaphore reais.
- [ ] Hardware-qualified exige execução no stack Vulkan real.
- [ ] Nenhuma assumption Vulkan sem base técnica verificável.

## Próxima fase

Fase 12.
