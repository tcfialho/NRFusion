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
- [ ] Validar extensions, size/alignment e handle ownership.
- [ ] Capturar/compose GPU-side.
- [ ] Context recreation/resize.
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
- [ ] Acquire/interop GPU-resident.
- [ ] OpenGL tocado respeita <=300 linhas por arquivo.

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
