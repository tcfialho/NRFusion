# Fase 12 — OpenGL carrier

## Objetivo

Finalizar SyntheticOpenGlProvider como rota GPU-resident.

## Dependências

Fases 07–08.

## Fora de escopo

- glReadPixels
- Claim sem extensions

## Checklist de implementação

- [ ] Integrar OpenGL no ProviderPolicy.
- [ ] Carrier present != qualified.
- [ ] Validar memory/semaphore extensions e context.
- [ ] Import shared D3D12 memory/fence.
- [ ] Capture GPU-side.
- [ ] Executor canônico.
- [ ] Compose/copy back GPU-side.
- [ ] Context recreation/resize.
- [ ] Frontend OpenGL mínimo.

## Revisão obrigatória

- [ ] GL object lifetime.
- [ ] Handle ownership.
- [ ] Sync ida/volta.
- [ ] Fallback sem extensions.
- [ ] Nunca CPU silencioso.

## Validação rápida

- [ ] OpenGL steady/resize/context recreation.
- [ ] Extension failure.
- [ ] Semaphore long run.

## Gate de conclusão

- [ ] Capability real controla policy.
- [ ] GPU-only route.
- [ ] Failure explícita.

## Entregáveis

- OpenGL carrier
- OpenGL harness

## Próxima fase

Fase 13.
