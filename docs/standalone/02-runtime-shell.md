# Fase 02 — Standalone runtime shell

## Objetivo

Criar lifecycle/bootstrap/configuração sem concentrar tudo em um `Runtime.cpp`, `GameProbe.cpp` ou build script gigante.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor real
- Carrier completo
- MFG completo

## Implementação

- [ ] Separar bootstrap/proxy, runtime lifecycle, config snapshot e registry.
- [ ] Se `GameProbe.cpp` for reaproveitado, separar PE/module inspection, API resolution, capability scan e install-support logic.
- [ ] `CMakeLists.txt` já está em ~267 linhas: modularizar Core/Tests/Windows/Standalone antes de adicionar muitos targets novos.
- [ ] Se `build_dist.ps1` precisar ser tocado antes do cutover, dividir primeiro; não fazê-lo crescer acima das 395 linhas atuais.
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
- [ ] Build modularization não duplica target definitions/options.

## Validação rápida

- [ ] Abrir/fechar shell com fake components.
- [ ] Falha por estágio.
- [ ] Medir disabled path.
- [ ] LOC checker inclui CMake/scripts.

## Gate

- [ ] Shutdown sem leak/thread.
- [ ] Disabled não altera output.
- [ ] Bootstrap/probe desacoplados de Acquire/Execute.
- [ ] Build files tocados respeitam <=300.

## Próxima fase

Fase 03.
