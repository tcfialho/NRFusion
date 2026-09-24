# Fase 11 — Vulkan carrier

## Objetivo

Transformar suporte Vulkan estrutural em Acquire→Normalize→Execute→Compose real sem provider monolítico.

## Dependências

Fases 07–08.

## Fora de escopo

- Fake handles como prova real
- Constantes Vulkan inventadas

## Implementação

- [x] Reusar `SyntheticVulkanProvider`; separar contract simulation de VkDevice real.
- [x] Se provider atual >300 for tocado, dividir loader/capabilities, interop/sync e carrier orchestration.
- [ ] Definir Acquire seam e ownership de VkImage.
- [ ] Usar headers Vulkan corretos/memoryTypeIndex real.
- [ ] External memory/semaphore/handle ownership explícitos.
- [ ] Layout/access/queue-family ownership.
- [ ] Compose back e recreation.
- [x] Cada arquivo handwritten <=300 linhas.

## Revisão obrigatória

- [x] Provider recebe handle != aquisição correta.
- [x] Fake-handle test é só contract test.
- [ ] Format/tiling/usage compatíveis.
- [ ] Sync completo success/failure.
- [ ] Sem CPU readback.

## Validação rápida

- [x] Contract tests sem Vulkan real.
- [ ] VkDevice real quando disponível.
- [ ] Recreate/semaphore long run.
- [x] LOC checker.

## Gate

- [ ] Harness-verified exige resources Vulkan reais.
- [ ] Acquire e interop têm evidência separada.
- [ ] Vulkan tocado respeita <=300 linhas por arquivo.

## Próxima fase

Fase 12.


## Subgate 11a — contrato portátil e fail-closed

- `VulkanCarrierContract` separa contract simulation do backend nativo;
- contrato valida identidade, extent, format, usage, allocation, memoryTypeBits/index,
  ownership de handle, layout intent, queue ownership e timeline wait/signal;
- fake handles existem apenas no contract test portátil e não contam como interop real;
- `SyntheticVulkanProvider.cpp` foi reduzido para 146 linhas;
- interop/sync/commands ficaram em `SyntheticVulkanProviderInterop.cpp` com 224 linhas;
- `ipc_host_test.cpp` caiu para 248 linhas ao remover o falso teste de interop;
- sem VkDevice/backend válido, provider/import/barrier/blit/submit agora falham fechado;
- o patcher legado continua copiando um `SyntheticVulkanProvider.cpp` autocontido,
  sem exigir mudança no patcher grandfathered;
- portable CI PASS no head `3566c14`;
- Windows hosted run `35947607432` ainda estava em andamento no fechamento desta janela.

## Próximo subgate

1. inspecionar o run Windows `35947607432`; não relançar se ainda estiver ativo;
2. introduzir um contexto Vulkan nativo explícito com instance, physical device,
   device, queue e queue family;
3. usar headers Vulkan oficiais, sem ABI/constantes locais;
4. consultar `vkGetMemoryWin32HandlePropertiesKHR` e intersectar memoryTypeBits reais;
5. só então reativar import/layout/compose Vulkan e criar gate VkDevice real.


## Subgate 11b — contexto nativo e memory type real (WIP)

Head submetido: `1d7d62a`.

Implementado:
- contexto explícito: instance, physical device, device, queue e queue family;
- owner nativo compilado contra `KhronosGroup/Vulkan-Headers` oficial;
- consulta de `vkGetMemoryWin32HandlePropertiesKHR`;
- seleção de memory type pela interseção entre requirements da imagem e bits do handle importado;
- removido o fallback `memoryTypeIndex = 0` do import;
- provider Vulkan só reporta ready com contexto completo e PFNs obrigatórios.

Validação pendente no freeze:
- portable run `35948880898`;
- Windows hosted run `35948880921`.

Não relançar esses runs sem antes inspecionar o estado existente.


## Subgate 11c — ABI oficial e VkDevice real (WIP)

Código totalmente validado: `47536ba`.

Concluído:
- removidas as structs/enums Vulkan inventadas do header público;
- lifecycle/dispatch, interop/sync e commands nativos estão em owners separados <=300 linhas;
- barrier/copy/blit usam tipos oficiais `VkImageMemoryBarrier`, `VkImageCopy` e `VkImageBlit`;
- import de memória usa `vkGetMemoryWin32HandlePropertiesKHR` e interseção real de memory type;
- patcher legado continua compilável sem SDK e fail-closed fora do build nativo;
- portable + Windows hosted: PASS em `47536ba`.

Harness real em `0470771`:
- cria VkInstance/VkPhysicalDevice/VkDevice/VkQueue reais;
- cria e aloca duas VkImages reais;
- grava transitions + copy pelo owner `VulkanNativeCommands`;
- submete na queue e espera apenas no harness de validação;
- portable CI PASS;
- Windows hosted run `35952257874`: PASS.

O harness VkDevice não substitui a evidência de external-memory/semaphore;
essa prova fica para o próximo subgate.
