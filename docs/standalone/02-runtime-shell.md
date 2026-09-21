# Fase 02 — Standalone runtime shell

## Objetivo

Criar lifecycle/bootstrap/configuração sem concentrar tudo em runtime, probe ou build script gigante.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor real
- Carrier completo
- MFG completo

## Implementação

- [ ] Separar bootstrap/proxy, runtime lifecycle, config snapshot e registry.
- [ ] Se `GameProbe.cpp` for reaproveitado, separar PE/module inspection, API resolution, capability scan e install-support logic.
- [ ] `CMakeLists.txt` (~267 linhas) é dividido antes de crescer: Core, Tests, Windows e Standalone em includes CMake coesos.
- [ ] Modularização CMake continua produzindo os mesmos targets; não criar configure/build extra.
- [ ] Se `build_dist.ps1` precisar ser tocado cedo, dividir antes; não crescer as ~395 linhas atuais.
- [ ] Definir init/shutdown idempotentes.
- [ ] RuntimeConfig compacto por snapshot/generation.
- [ ] Registrar providers/executors por capability.
- [ ] Logging/status mínimo.
- [ ] Eliminar OptiScaler Config/State e patcher Python em runtime.
- [ ] Disabled quase pass-through.
- [ ] Cada módulo <=300; alvo <=250.

## Revisão obrigatória

- [ ] COM/module/hook ownership explícito.
- [ ] Partial init/shutdown limpa só o que possui.
- [ ] Bootstrap não pressupõe API.
- [ ] Probing não mistura filesystem com frame runtime.
- [ ] Build split não duplica target definitions/options/jobs.

## Validação rápida

- [ ] Abrir/fechar shell com fake components.
- [ ] Falha por estágio.
- [ ] Medir disabled path.
- [ ] LOC checker inclui CMake/scripts.

## Gate

- [ ] Shutdown sem leak/thread.
- [ ] Disabled não altera output.
- [ ] Bootstrap/probe desacoplados de Acquire/Execute.
- [ ] Build files tocados <=300 sem aumentar número de builds.

## Próxima fase

Fase 03.
