# Fase 12 — OpenGL carrier

## Objetivo

Qualificar OpenGL→D3D12 sem glReadPixels e sem manter `SyntheticOpenGlProvider.cpp` grande.

## Dependências

Fases 07–08.

## Fora de escopo

- Teste lógico como prova de interop real
- CPU pixel path

## Implementação

- [x] Reusar provider/teste existentes.
- [x] Antes de expansão, dividir loader/extensions, GL-D3D12 interop/sync e carrier orchestration.
- [ ] Definir Acquire seam em contexto GL real.
- [x] ProviderPolicy só atrás de capability comprovada.
- [x] Validar extensions, size/alignment e handle ownership.
- [x] Capturar/compose GPU-side.
- [x] Context recreation/resize implementado; evidência física pendente.
- [x] Cada arquivo <=300 linhas.

## Revisão obrigatória

- [x] Initialize sem contexto GL não prova interop.
- [x] GL objects/HANDLEs têm lifetime pareado no owner atual.
- [x] Sem CPU stall como normal.
- [x] Extension ausente = Blocked.
- [x] Split segue lifetime GL/D3D12, não tamanho arbitrário.

## Validação rápida

- [x] Teste lógico barato.
- [ ] Contexto real quando disponível.
- [ ] Recreate/semaphore long run.
- [x] LOC checker.

## Gate

- [x] ProviderPolicy só seleciona rota comprovada.
- [x] Acquire/interop GPU-resident; qualificação física pendente.
- [x] OpenGL tocado respeita <=300 linhas por arquivo.

## Próxima fase

Fase 13.


## Subgate 12a — fail-closed provider split + Acquire contract (WIP)

A Fase 12 foi iniciada porque a Fase 11 tem blocker físico explícito. A Fase 11
continua aberta e seu gate físico permanece obrigatório.

Código:
- `875e000` — split do provider, fail-closed sem contexto GL e remoção do wait
  de CPU no ring;
- `ce244cd` — Acquire portátil + gating de ProviderPolicy;
- `35af18c` — include corrigido para os tipos de work portáteis.

Implementado:
- `SyntheticOpenGlProviderInterop.cpp` passou a ser owner de import/copy/sync e
  shared resources;
- orchestration não usa mais `WaitForSingleObject` quando o slot está ocupado;
- `Initialize` exige API OpenGL, `HGLRC` atual e entry points de interop;
- teste headless agora prova fail-closed, não interop real;
- `OpenGlCarrierAcquire` recebe contexto e capability facts explícitos,
  `resourceGeneration`, texture facts e provenance;
- somente RGBA16F Texture2D 1x sample/1 mip é aceito no contrato atual;
- `ProviderPolicy` só libera Synthetic OpenGL quando `openGlCarrier=true`;
- capabilities synthetic de outras APIs não desbloqueiam OpenGL.

Validação:
- Windows `synthetic_opengl_test` PASS no batch `875e000`;
- focused portable `36060926802`: PASS;
- source checkpoint:
  `nrfusion-source-35af18c381695413ffe61b9703f25bb38e473322`;
- Windows `36060926786`: ainda em andamento no freeze;
- nenhum contexto GL real foi usado como evidência.

### Finding de sync para 12b

A especificação oficial dos external objects Win32 exige valor explícito para
semaphore importado de `D3D12_FENCE`. O provider atual ainda precisa:
1. validar nomes das extensions, não apenas PFNs;
2. programar `GL_D3D12_FENCE_VALUE_EXT` por operação;
3. fazer a queue D3D12 esperar pelo sinal GL de input;
4. sinalizar completion D3D12 com valor consumido pelo wait GL de output;
5. ligar esses valores a uma identidade de slot/work explícita, sem depender de
   `currentSlot_` implícito.

Até esse subgate e um harness com contexto GL real passarem,
`IntegratedCapabilities().openGlCarrier` deve continuar `false`.


## Subgate 12b — explicit GL/D3D12 fence synchronization

Código validado: `1c4eff5`.

Implementado:
- identidade explícita `OpenGlCarrierSyncIdentity` por work/slot;
- três valores de fence por work: GL input-ready, D3D12 output-ready e GL release;
- validação dos nomes anunciados `GL_EXT_memory_object`,
  `GL_EXT_memory_object_win32`, `GL_EXT_semaphore` e
  `GL_EXT_semaphore_win32`;
- `GL_D3D12_FENCE_VALUE_EXT` programado antes de signal/wait;
- input: GL GPU copy -> GL signal -> D3D12 queue wait -> D3D12 work;
- output: D3D12 queue signal -> GL wait -> GL GPU copy -> GL release signal;
- um `ID3D12Fence` compartilhado por slot, evitando retirement cruzado;
- slot sem output consume não é reutilizado;
- nenhum `WaitForSingleObject`, readback ou wait de CPU foi reintroduzido.

Correções durante validação:
- `3e10c91`: implementação inicial;
- Windows detectou referência stale a `SharedSlot::workId`;
- `6ae7647`: correção mecânica;
- revisão estática detectou que um fence global permitia avanço por outro slot;
- `1c4eff5`: fence/handle isolados por slot.

Validação:
- focused portable `36074539887`: PASS;
- source-size: PASS;
- checkpoint:
  `nrfusion-source-1c4eff5f9cee76dd3a88d65dfcfe339c47191c3f`;
- Windows hosted `36074539924`: PASS;
- `nrfusion_synthetic_opengl_test`: PASS;
- 10/10 testes targeted: PASS;
- nenhum contexto WGL com external objects foi usado como evidência.

A semântica implementada segue a especificação Khronos EXT: semaphore importado de
D3D12 fence usa valor explícito de fence, e wait/signal GL permanecem operações
server/GPU-side.

### Próximo subgate

1. criar harness de contexto WGL real e verificar extensions anunciadas;
2. provar GL input copy -> D3D12 result -> GL output consume;
3. exercitar resize/context recreation e long-run;
4. criar gate físico Phase 12 que não aceite SKIP;
5. somente depois habilitar `IntegratedCapabilities().openGlCarrier`.


## Subgate 12c — real WGL external-object harness (WIP)

Código:
- `ad639e3` — harness WGL real + gate físico + same-adapter LUID;
- `ef726da` — split do harness para respeitar <=300 linhas;
- `043c414` — rejeita sentinelas inválidas de `wglGetProcAddress`.

Implementado:
- contexto WGL real em hidden window `CS_OWNDC`;
- query `GL_DEVICE_LUID_EXT` e seleção do `IDXGIAdapter1` correspondente;
- provider OpenGL cria D3D12 no mesmo adapter do contexto GL;
- import de `D3D12_RESOURCE` usa `size=0`;
- recursos RGBA16F D3D12 compartilhados são importados como GL textures;
- D3D12 fence compartilhado é importado como GL semaphores;
- ciclo real do harness:
  GL copy -> GL signal -> D3D12 queue wait -> D3D12 copy ->
  D3D12 signal -> GL wait -> GL copy -> GL release signal;
- 32 ciclos de resource/resize recreation;
- 128 ciclos reutilizando os mesmos imports;
- fechamento e recriação completa do contexto WGL;
- readback existe somente no harness para validação de conteúdo, não no runtime;
- `validate_phase12_hardware.ps1` torna hardware obrigatório e executa o
  binário diretamente, portanto exit 77 não pode virar PASS físico.

Source-size:
- `SyntheticOpenGlProvider.hpp`: 228;
- lifecycle: 263;
- interop: 237;
- context harness: 238;
- resources harness: 206;
- run harness: 176;
- `NRFusionWindows.cmake`: 293;
- novo CMake OpenGL: 35;
- gate PowerShell: 40.

Validação no freeze:
- focused portable `36076073284`: em andamento;
- Windows hosted `36076073277`: em andamento;
- não relançar esses workflows sem primeiro inspecionar o estado existente.

### Finding de produção ainda aberto

O harness prova a infraestrutura de external objects com um D3D12 copy real, mas
o provider ainda não publica o resultado neural em `slot.d3d12Residual`.
`SyntheticDx12Provider::Submit` prepara `lowColor`; ele não produz o residual
final. Portanto o output-ready do provider não deve ser tratado como resultado
válido até existir um passo explícito, work-bound, que copie/publique o resultado
D3D12 real no shared output e só então sinalize o fence de saída.

`IntegratedCapabilities().openGlCarrier` continua obrigatoriamente `false`.

### Próximo subgate

1. inspecionar `36076073284` e `36076073277`;
2. introduzir publicação D3D12 do resultado real ligada ao work/slot;
3. mover o output-ready signal para depois dessa publicação;
4. executar o gate físico Phase 12 quando hardware local voltar;
5. só depois avaliar capability integrada.


## Subgate 12d — publish do resultado D3D12 real

Código validado: `fdec6dc`.

Mudança de ownership:
- `Submit` faz apenas acquisition/cópia de input e prepara o work D3D12;
- `GetD3D12Work` entrega device/queue, `lowColor` e `lowNeuralOut`
  ligados ao mesmo work/slot;
- o caller executa o modelo real na mesma queue;
- `PublishD3D12Result` executa `ExtractResidual`, compõe o resultado
  native-resolution no recurso compartilhado com GL e só então sinaliza
  `outputSignalValue`;
- `Poll`, `GetResidual` e output consume falham antes do publish.

Correctness:
- outer slot segue o próximo ring slot do `SyntheticDx12Provider`, evitando
  overwrite de um inner slot ainda vivo quando a liberação externa ocorre fora
  de ordem;
- cada slot possui allocator separado para publish;
- resize/recreation retorna blocked enquanto qualquer release GL anterior não
  estiver aposentado;
- shared output representa resultado composto final; `GetResidual` continua
  expondo o residual interno correto;
- nenhum wait/readback de CPU foi introduzido no runtime normal.

Harness físico:
- além do transporte independente, executa 32 ciclos pelo
  `SyntheticOpenGlProvider` real;
- usa copy D3D12 como modelo identidade para escrever `lowNeuralOut`;
- publica pelo novo método e valida o resultado após o consume GL;
- o readback continua restrito ao harness.

Validação:
- focused portable `36077955388`: PASS;
- source-size: PASS;
- checkpoint:
  `nrfusion-source-fdec6dcae9a6de16137c45c7c90ba9d1c2800d94`;
- Windows hosted `36077955377`: PASS;
- `nrfusion_synthetic_opengl_test`: PASS;
- `nrfusion_opengl_external_interop_tests`: SKIP 77;
- o SKIP hosted não é evidência física.

## Estado da Fase 12

A implementação verificável sem hardware está congelada. A Fase 12 permanece
**IN PROGRESS** somente pela ausência do gate WGL/D3D12 físico.

Ação física única:
`tools/validate_phase12_hardware.ps1`

O script executa o binário diretamente com hardware obrigatório; exit 77 não é
aceito como sucesso.

Nota de capability: `GameProbe::IntegratedCapabilities()` descreve hooks
realmente distribuídos. Como ainda não existe hook OpenGL enviado pelo patcher,
`openGlCarrier` deve permanecer `false` mesmo se o carrier da Fase 12 passar
fisicamente. A habilitação anunciada pertence ao gate posterior de hook/cutover,
não à prova isolada do carrier.
