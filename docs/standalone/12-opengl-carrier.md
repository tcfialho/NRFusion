# Fase 12 — OpenGL carrier

## Objetivo

Transformar o provider OpenGL atual em interop real qualificado, sem glReadPixels.

## Dependências

Fases 07–08.

## Fora de escopo

- Considerar o teste lógico atual como prova de GL↔D3D12 real
- CPU pixel path

## Implementação

- [ ] Reusar `SyntheticOpenGlProvider` e `nrfusion_synthetic_opengl_test`.
- [ ] Integrar OpenGL no ProviderPolicy somente atrás de capability real.
- [ ] Criar contexto GL mínimo no teste apenas para validar memory object/semaphore import de verdade.
- [ ] Validar extensions, memory size/alignment e handle ownership.
- [ ] Capturar color GPU-side e guides quando disponíveis.
- [ ] Executar D3D12 canônico e compor/copy back GPU-side.
- [ ] Tratar context recreation e resize.

## Revisão obrigatória

- [ ] Provider.Initialize sem contexto GL não equivale a interop qualificado.
- [ ] GL objects e HANDLEs têm lifetime pareado.
- [ ] Sync ida/volta não usa stall CPU como normal.
- [ ] Extension ausente = Blocked explícito, nunca fallback via CPU.

## Validação rápida

- [ ] Manter teste lógico barato para lifecycle.
- [ ] Adicionar teste com contexto/extensões reais quando ambiente permitir.
- [ ] Context recreation e semaphore/fence long run.

## Gate

- [ ] ProviderPolicy só seleciona OpenGL quando capability real foi comprovada.
- [ ] Rota normal permanece GPU-resident.

## Próxima fase

Fase 13.
