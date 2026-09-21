# Fase 02 — Standalone runtime shell

## Objetivo

Criar lifecycle, configuração e registro de componentes sem ainda executar DLSS 5.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor real
- MFG completo
- Advanced completo

## Implementação

- [ ] Definir init/shutdown idempotentes.
- [ ] RuntimeConfig compacto por snapshot/generation.
- [ ] Registrar providers/executors por capability, não por preferência implícita.
- [ ] Logging/status mínimo com failure reason estável.
- [ ] Eliminar OptiScaler Config/State do novo shell.
- [ ] Eliminar patcher Python como requisito de runtime.
- [ ] Disabled path quase pass-through.
- [ ] Definir thread ownership e transições de estado do runtime.

## Revisão obrigatória

- [ ] COM/module/hook ownership explícito.
- [ ] Partial init e shutdown limpam apenas o que possuem.
- [ ] Sem filesystem/config parsing no frame path.
- [ ] Global mutable state precisa de owner e motivo.

## Validação rápida

- [ ] Abrir/fechar repetidamente com FakeNrExecutor.
- [ ] Injetar falha em cada estágio de init.
- [ ] Medir disabled path no harness.

## Gate

- [ ] Shutdown sem leak/thread pendente.
- [ ] Disabled não altera FrameContract/output.
- [ ] Config só é recompilada quando generation muda.

## Próxima fase

Fase 03.
