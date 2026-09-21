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
