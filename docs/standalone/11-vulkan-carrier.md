# Fase 11 — Vulkan carrier

## Objetivo

Transformar suporte Vulkan estrutural em Acquire→Normalize→Execute→Compose real sem provider monolítico.

## Dependências

Fases 07–08.

## Fora de escopo

- Fake handles como prova real
- Constantes Vulkan inventadas

## Implementação

- [ ] Reusar `SyntheticVulkanProvider`; separar contract simulation de VkDevice real.
- [ ] Se provider atual >300 for tocado, dividir loader/capabilities, interop/sync e carrier orchestration.
- [ ] Definir Acquire seam e ownership de VkImage.
- [ ] Usar headers Vulkan corretos/memoryTypeIndex real.
- [ ] External memory/semaphore/handle ownership explícitos.
- [ ] Layout/access/queue-family ownership.
- [ ] Compose back e recreation.
- [ ] Cada arquivo handwritten <=300 linhas.

## Revisão obrigatória

- [ ] Provider recebe handle != aquisição correta.
- [ ] Fake-handle test é só contract test.
- [ ] Format/tiling/usage compatíveis.
- [ ] Sync completo success/failure.
- [ ] Sem CPU readback.

## Validação rápida

- [ ] Contract tests sem Vulkan real.
- [ ] VkDevice real quando disponível.
- [ ] Recreate/semaphore long run.
- [ ] LOC checker.

## Gate

- [ ] Harness-verified exige resources Vulkan reais.
- [ ] Acquire e interop têm evidência separada.
- [ ] Vulkan tocado respeita <=300 linhas por arquivo.

## Próxima fase

Fase 12.
