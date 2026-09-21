# Fase 02 — Standalone runtime shell

## Objetivo

Criar host/runtime mínimo sem executar NR.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor DLSS 5
- MFG completo
- Advanced

## Checklist de implementação

- [ ] Lifecycle init/shutdown.
- [ ] RuntimeConfig compacto por snapshot/generation.
- [ ] Registro de providers/executors.
- [ ] Logging/status mínimo.
- [ ] Sem OptiScaler Config/State.
- [ ] Sem patcher Python em runtime.
- [ ] Disabled path quase pass-through.
- [ ] Thread ownership e error model explícitos.

## Revisão obrigatória

- [ ] COM/module ownership.
- [ ] Hook install/remove.
- [ ] Partial init/shutdown.
- [ ] Global mutable state.
- [ ] Sem file/config parsing no frame path.

## Validação rápida

- [ ] Abrir/fechar shell repetidamente.
- [ ] Forçar falhas de init e revisar cleanup.
- [ ] Medir disabled overhead no Harness.

## Gate de conclusão

- [ ] Shutdown sem leaks/threads.
- [ ] Disabled não altera render.
- [ ] RuntimeConfig sem parsing/frame.

## Entregáveis

- Standalone shell
- Lifecycle contract

## Próxima fase

Fase 03.
