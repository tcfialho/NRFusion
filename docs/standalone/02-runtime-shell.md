# Fase 02 — Standalone runtime shell

## Objetivo

Criar lifecycle/bootstrap/configuração sem concentrar tudo em um `Runtime.cpp` ou `GameProbe.cpp` gigante.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor real
- Carrier completo
- MFG completo

## Implementação

- [ ] Separar bootstrap/proxy, runtime lifecycle, config snapshot e registry.
- [ ] Se `GameProbe.cpp` for reaproveitado, separar PE/module inspection, API resolution, capability scan e install-support logic antes de expansão.
- [ ] Definir init/shutdown idempotentes.
- [ ] RuntimeConfig compacto por snapshot/generation.
- [ ] Registrar providers/executors por capability.
- [ ] Logging/status mínimo.
- [ ] Eliminar OptiScaler Config/State e patcher Python em runtime.
- [ ] Disabled quase pass-through.
- [ ] Cada módulo <=300 linhas; alvo <=250.

## Revisão obrigatória

- [ ] COM/module/hook ownership explícito.
- [ ] Partial init/shutdown limpa só o que possui.
- [ ] Bootstrap não pressupõe API.
- [ ] Probing não mistura filesystem parsing com frame runtime.
- [ ] Nenhuma classe agrega bootstrap + policy + carrier.

## Validação rápida

- [ ] Abrir/fechar shell com fake components.
- [ ] Falha por estágio.
- [ ] Medir disabled path.
- [ ] LOC checker.

## Gate

- [ ] Shutdown sem leak/thread.
- [ ] Disabled não altera output.
- [ ] Bootstrap/probe desacoplados de Acquire/Execute.
- [ ] Nenhum arquivo novo/tocado >300.

## Próxima fase

Fase 03.
