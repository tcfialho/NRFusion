# Fase 12 — OpenGL carrier

## Objetivo

Qualificar OpenGL→D3D12 usando external memory/semaphore, sem glReadPixels.

## Dependências

Fases 07–08.

## Fora de escopo

- CPU pixel path
- Claim sem extensions

## Implementação

- [ ] Integrar OpenGL no ProviderPolicy apenas atrás de capability real.
- [ ] Validar context e extensions de memory/semaphore.
- [ ] Importar shared D3D12 resources/fence com ownership explícito.
- [ ] Capturar color GPU-side e guides apenas quando tecnicamente obtíveis.
- [ ] Executar D3D12 canônico e compor/copy back GPU-side.
- [ ] Tratar context recreation e resize.
- [ ] Adicionar frontend OpenGL mínimo ao runner.

## Revisão obrigatória

- [ ] Carrier present != route qualified.
- [ ] GL objects e HANDLEs têm lifetime pareado.
- [ ] Sincronização ida/volta não usa stall CPU como normal.
- [ ] Sem extension necessária = Blocked explícito, não fallback CPU.

## Validação rápida

- [ ] Harness OpenGL com fake executor.
- [ ] Context recreation e extension failure.
- [ ] Long run de semaphore/fence values.

## Gate

- [ ] ProviderPolicy só seleciona OpenGL quando capability foi realmente qualificada.
- [ ] Rota normal permanece GPU-resident.

## Próxima fase

Fase 13.
