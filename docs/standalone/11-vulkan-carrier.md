# Fase 11 — Vulkan carrier

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose em Vulkan, além do provider simulado atual.

## Dependências

Fases 07–08.

## Fora de escopo

- Tratar fake handles como interop real
- Constantes Vulkan não verificadas

## Implementação

- [ ] Reusar `SyntheticVulkanProvider`, distinguindo contract simulation de VkDevice real.
- [ ] Definir hooks/seam de Acquire no jogo Vulkan e ownership de VkImage.
- [ ] Usar headers Vulkan corretos e memoryTypeIndex real.
- [ ] Definir external-memory compatibility, semaphore type e handle ownership.
- [ ] Definir layouts/access/queue-family ownership.
- [ ] Normalizar guides com provenance, executar D3D12 e compor de volta.
- [ ] Tratar swapchain/device recreation.
- [ ] Estender harness somente para gaps não cobertos.

## Revisão obrigatória

- [ ] Provider que recebe image/handle não prova aquisição correta do jogo.
- [ ] Fake-handle test é só contract test.
- [ ] Format/tiling/usage compatíveis com interop.
- [ ] Sync/ownership completos em success/failure.
- [ ] Sem CPU readback.

## Validação rápida

- [ ] Contract tests sem Vulkan real.
- [ ] Harness com VkDevice real quando runtime/extensões existirem.
- [ ] Resize/recreate/semaphore long run.

## Gate

- [ ] Harness-verified exige resources Vulkan reais.
- [ ] Hardware-qualified exige stack Vulkan + jogo/engine real.
- [ ] Acquire e interop têm evidência separada.

## Próxima fase

Fase 12.
