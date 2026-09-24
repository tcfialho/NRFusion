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
- [x] Compose back e recreation implementados; prova física continua pendente.
- [x] Cada arquivo handwritten <=300 linhas.

## Revisão obrigatória

- [x] Provider recebe handle != aquisição correta.
- [x] Fake-handle test é só contract test.
- [x] Format/tiling/usage compatíveis.
- [x] Sync success/failure implementado no contrato/harness; runtime físico pendente.
- [x] Sem CPU readback.

## Validação rápida

- [x] Contract tests sem Vulkan real.
- [x] VkDevice real quando disponível.
- [ ] Recreate/semaphore long run.
- [x] LOC checker.

## Gate

- [ ] Harness-verified exige resources Vulkan reais.
- [x] Acquire e interop têm evidência separada.
- [x] Vulkan tocado respeita <=300 linhas por arquivo.

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


## Subgate 11f — execution plan portátil (WIP)

Código submetido:
- `ea7098c` — consumo explícito do claim no ledger `NrSession`;
- `c12cb6c` — `VulkanCarrierExecutionPlan` + fatos nativos preservados;
- `d49c898` — testes portáteis + focused gate.

Implementado:
- plan exige `VulkanCarrierWork` válido e `WorkTicket` previamente claimed;
- o claim continua pertencendo ao ledger do `NrSession`; não existe ledger Vulkan paralelo;
- consumo do claim é one-shot, permitindo ao executor futuro rejeitar execução duplicada;
- color/output mantêm opaque id, extent, format, usage, layout e queue family explícitos;
- provenance do color é validada contra o `FrameContext` adquirido;
- recreation generation e compose intent são explícitos;
- external interop mantém queue ownership e producer-wait/consumer-signal timeline contracts no plan;
- falhas são fail-closed antes de qualquer recording/submission.

Cobertura portátil:
- work stale;
- runtime generation mismatch;
- queue-family mismatch;
- layout/usage/dimensions inválidos;
- resource identity/provenance mismatch;
- execution before claim;
- duplicate execution consumption;
- abandon path;
- valid local plan e valid external-sync plan.

Source-size manual:
- `VulkanCarrierExecutionPlan.hpp`: 96 linhas;
- `VulkanCarrierExecutionPlan.cpp`: 176 linhas;
- `vulkan_carrier_execution_plan_tests.cpp`: 186 linhas;
- demais arquivos de implementação tocados também permanecem <=300.

Validação:
- focused portable run `36012347260`: PASS;
- Windows hosted run `36012347315`: PASS;
- source-size: PASS;
- source checkpoint `nrfusion-source-d49c898b86e1617dbc025fcf714259e0420a7691` publicado;
- nenhuma evidência física foi produzida; acesso local continua desabilitado.

Próximo incremento:
1. implementar `VulkanCarrierExecutor` nativo sobre `VkCommandBuffer` do caller;
2. consumir o claim exatamente uma vez no início do recording;
3. gravar transitions/acquire/compose/release sem assumir queue submission;
4. manter waits bloqueantes, CPU readback e fallback DX12 fora do caminho standalone;
5. manter compose/recreation real e prova física como gates separados.


## Subgate 11g — VulkanCarrierExecutor nativo (WIP)

Código:
- `92411f1` — executor + layouts explícitos + dedicated Windows test;
- `9958475` — isola o executor do object library usado pelo external interop.

Implementado:
- recebe `VkCommandBuffer` do caller e não assume queue submission;
- valida plan/context/command buffer/dispatch/images/queue family antes de recording;
- consome o claim do `NrSession` exatamente uma vez no início do recording;
- falha antes do recording preserva o claim para abandon/retry controlado pelo caller;
- local: transition -> copy/blit -> restore;
- external: acquire de `VK_QUEUE_FAMILY_EXTERNAL` -> copy/blit -> release externo;
- source/destination layouts do copy/blit são explícitos;
- producer wait e consumer signal permanecem no plan para a submissão do caller;
- nenhum wait de queue/fence, CPU readback ou fallback DX12 foi adicionado.

Hosted evidence:
- focused portable run `36023547892`: PASS;
- source checkpoint `nrfusion-source-9958475155e0f9c0000f9f20f8a8973770842bfd` publicado;
- primeira Windows run `36023092730`: FAIL somente por link do external interop,
  porque o executor foi incluído no object library compartilhado;
- causa corrigida em `9958475`, sem mudança de runtime;
- Windows run `36023547968`: PASS, incluindo `nrfusion_vulkan_carrier_executor_tests`;
- external physical runtime continua sem evidência nesta sessão.

Próximo incremento:
1. usar o harness `VkDevice` real existente para gravar pelo executor;
2. manter submit/wait apenas no harness de validação, fora do executor;
3. depois avançar compose/recreation nativo como responsabilidade separada;
4. manter external-memory/semaphore físico como gate separado.


## Subgate 11h — recreation generation + physical-ready gates

Código hosted-validado: `a4593eb`.

Implementado:
- `resourceGeneration` nasce no Acquire e é preservado no `VulkanAcquireResult`;
- o execution plan rejeita `recreationGeneration` diferente da geração adquirida;
- `nrfusion_vulkan_carrier_device_tests` usa `VkImage`/`VkCommandBuffer`
  reais quando existe runtime Vulkan e executa 32 ciclos de criação, recording,
  submission pelo caller e destruição;
- o executor continua sem queue submission, wait bloqueante ou CPU readback;
- compose-back é Vulkan nativo por copy/blit para a imagem output do caller;
- o external harness mantém 32 ciclos de reimport/recreation e adiciona
  128 submissões reutilizando os mesmos imports e timeline semaphores;
- timeline inválida é coberta por falha explícita no plan.

Hosted validation:
- focused portable `36035658289`: PASS;
- Windows `36035658103`: PASS geral;
- `nrfusion_vulkan_native_device_tests`: PASS;
- `nrfusion_vulkan_carrier_executor_tests`: PASS;
- `nrfusion_vulkan_carrier_device_tests`: SKIP 77;
- `nrfusion_vulkan_external_interop_tests`: SKIP 77;
- source checkpoint:
  `nrfusion-source-a4593eb934027f8c3ff9572b22f43ae0e9647f40`;
- nenhum SKIP conta como evidência de runtime físico.

## Estado da Fase 11

A Fase 11 **não está encerrada**.

O trabalho de implementação verificável em hosted CI está congelado. Restam gates
físicos que não podem ser executados enquanto o acesso local estiver desabilitado:

1. executar `nrfusion_vulkan_carrier_device_tests` com
   `NRFUSION_TEST_VULKAN_HARDWARE=1`;
2. executar `nrfusion_vulkan_external_interop_tests` com
   `NRFUSION_TEST_VULKAN_EXTERNAL_HARDWARE=1`;
3. exigir execução real dos 32 ciclos de recreation e 128 ciclos de reuse;
4. só após PASS físico marcar `Recreate/semaphore long run`,
   `Harness-verified` e a própria Fase 11 como concluídos.
