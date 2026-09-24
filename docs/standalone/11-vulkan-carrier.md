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
- [x] Definir Acquire seam e ownership de VkImage.
- [x] Usar headers Vulkan corretos/memoryTypeIndex real.
- [x] External memory/semaphore/handle ownership explícitos.
- [x] Layout/access/queue-family ownership.
- [ ] Compose back e recreation.
- [x] Cada arquivo handwritten <=300 linhas.

## Revisão obrigatória

- [x] Provider recebe handle != aquisição correta.
- [x] Fake-handle test é só contract test.
- [x] Format/tiling/usage compatíveis.
- [ ] Sync completo success/failure.
- [x] Sem CPU readback.

## Validação rápida

- [x] Contract tests sem Vulkan real.
- [x] VkDevice real quando disponível.
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


## Subgate 11d — external memory/semaphore hosted interop (WIP)

Código validado: `35b6272`.

Implementado:
- produtor D3D12 real com duas textures compartilháveis, producer/consumer fences,
  adapter LUID e allocation size;
- import Vulkan valida `vkGetPhysicalDeviceImageFormatProperties2` para
  format/tiling/usage + `D3D12_RESOURCE`;
- handles Win32 recebidos permanecem do caller; o provider importa cópias duplicadas
  e fecha essas cópias após o import criar sua própria referência;
- harness externo casa o `VkPhysicalDevice` com o adapter D3D12 via LUID;
- device Vulkan do harness habilita external-memory Win32, external-semaphore Win32
  e timeline semaphore;
- harness importa duas textures e dois fences e valida o desenho
  producer wait -> consumer signal;
- target continua focado, sem linkar `nrfusion_core` inteiro.

Validação no freeze:
- portable run `35957702217`: PASS, incluindo LOC/checkpoint artifact;
- Windows run `35957702242`: PASS;
- nenhum gate físico foi executado porque o acesso local está desabilitado.

Próximo incremento hosted:
1. validar capability de external semaphore no provider;
2. adquirir/liberar ownership de imagem com `VK_QUEUE_FAMILY_EXTERNAL`;
3. usar os imports em command buffer real no harness;
4. stress de recreation/long-run;
5. manter a prova física separada e pendente.


## Subgate 11e — Acquire + ledger + external ownership

Código validado: `e7be568`.
Gate/workflow head: `b63d6f6`.

Concluído:
- `vkGetPhysicalDeviceExternalSemaphoreProperties` valida importabilidade de
  `D3D12_FENCE` antes do import;
- acquire/release externo usa barriers explícitos
  `VK_QUEUE_FAMILY_EXTERNAL <-> queue family local`;
- harness externo grava acquire -> copy GPU -> release e contém 32 ciclos de
  recreation; em runner sem interop real ele é CTest SKIP 77, não falso PASS;
- `VulkanCarrierAcquire` separa Acquire de interop e exige fatos explícitos de
  dimensão, formato, usage, layout, queue family e provenance;
- `VulkanCarrierSession` integra Vulkan ao mesmo `NrSession/WorkTicket`
  claim/submit/abandon usado no carrier D3D12;
- nenhum caminho novo usa CPU readback ou espera de GPU no runtime normal.

Validação:
- Windows hosted do código `e7be568`: PASS;
- focused portable final run `35996493589`: 18/18 PASS;
- contract/acquire/session Vulkan foram construídos e executados explicitamente;
- source-size: PASS;
- source checkpoint gerado e enviado como artifact pelo GitHub Actions;
- external-interoperability runtime continua sem evidência hosted porque o runner
  não possui combinação D3D12/Vulkan adequada e o teste foi SKIPPED;
- acesso local permaneceu desabilitado, portanto nenhum gate físico foi tentado.

Próximo subgate:
1. execução standalone Vulkan sobre caller-owned `VkCommandBuffer`;
2. execution plan ligado ao `VulkanCarrierWork`;
3. compose-back/recreation sem fallback DX12;
4. physical external-memory/semaphore gate somente quando acesso local voltar.
