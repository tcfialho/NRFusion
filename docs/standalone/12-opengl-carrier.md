# Fase 12 — OpenGL carrier

## Objetivo

Qualificar OpenGL→D3D12 sem glReadPixels e sem manter `SyntheticOpenGlProvider.cpp` grande.

## Dependências

Fases 07–08.

## Fora de escopo

- Teste lógico como prova de interop real
- CPU pixel path

## Implementação

- [ ] Reusar provider/teste existentes.
- [ ] Antes de expansão, dividir loader/extensions, GL-D3D12 interop/sync e carrier orchestration.
- [ ] Definir Acquire seam em contexto GL real.
- [ ] ProviderPolicy só atrás de capability comprovada.
- [ ] Validar extensions, size/alignment e handle ownership.
- [ ] Capturar/compose GPU-side.
- [ ] Context recreation/resize.
- [ ] Cada arquivo <=300 linhas.

## Revisão obrigatória

- [ ] Initialize sem contexto GL não prova interop.
- [ ] GL objects/HANDLEs têm lifetime pareado.
- [ ] Sem CPU stall como normal.
- [ ] Extension ausente = Blocked.
- [ ] Split segue lifetime GL/D3D12, não tamanho arbitrário.

## Validação rápida

- [ ] Teste lógico barato.
- [ ] Contexto real quando disponível.
- [ ] Recreate/semaphore long run.
- [ ] LOC checker.

## Gate

- [ ] ProviderPolicy só seleciona rota comprovada.
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
