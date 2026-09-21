# Fase 12 — OpenGL carrier

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose em OpenGL sem glReadPixels.

## Dependências

Fases 07–08.

## Fora de escopo

- Considerar teste lógico atual como interop real
- CPU pixel path

## Implementação

- [ ] Reusar `SyntheticOpenGlProvider` e teste existente.
- [ ] Definir hook/seam de Acquire em contexto OpenGL real.
- [ ] Integrar ProviderPolicy apenas atrás de capability comprovada.
- [ ] Criar contexto GL mínimo no teste para memory object/semaphore import real.
- [ ] Validar extensions, size/alignment e handle ownership.
- [ ] Capturar color GPU-side; guides somente se realmente obtíveis.
- [ ] Executar D3D12 e compor/copy back GPU-side.
- [ ] Tratar context recreation/resize.

## Revisão obrigatória

- [ ] Provider.Initialize sem contexto GL não prova Acquire/interoperabilidade.
- [ ] GL objects/HANDLEs têm lifetime pareado.
- [ ] Sync ida/volta não usa stall CPU como normal.
- [ ] Extension ausente = Blocked, nunca fallback CPU.

## Validação rápida

- [ ] Teste lógico continua barato.
- [ ] Teste de contexto real prova interop quando ambiente permite.
- [ ] Context recreation/semaphore long run.

## Gate

- [ ] ProviderPolicy só seleciona rota comprovada.
- [ ] Acquire e interop são ambos GPU-resident.

## Próxima fase

Fase 13.
