# Fase 11 — Vulkan carrier

## Objetivo

Qualificar Vulkan -> executor D3D12.

## Dependências

Fases 07–08.

## Fora de escopo

- Compile-only support
- Hard-code Vulkan não verificado

## Checklist de implementação

- [ ] External memory Win32.
- [ ] Timeline semaphore import.
- [ ] Memory type correto.
- [ ] Image usage/layout.
- [ ] Queue-family ownership.
- [ ] Color/depth/motion.
- [ ] Provenance.
- [ ] Compose back.
- [ ] Swapchain/device recreation.
- [ ] Frontend Vulkan mínimo.

## Revisão obrigatória

- [ ] Headers/spec sustentam structs/constants.
- [ ] Handle ownership.
- [ ] Layout/queue transition.
- [ ] Sem CPU readback.
- [ ] Failure limpa.

## Validação rápida

- [ ] Vulkan harness steady/resize/recreate.
- [ ] Interop failure.
- [ ] Timeline long run.

## Gate de conclusão

- [ ] GPU-resident route validada.
- [ ] Sem assumption não verificada.
- [ ] 0 resource creation steady.

## Entregáveis

- Vulkan carrier
- Vulkan harness

## Próxima fase

Fase 12.
