# Fase 22 — Matriz de qualificação por API/bitness

## Objetivo

Dar evidência reproduzível por rota e chegar à RC sem dívida estrutural.

## Dependências

Rotas candidatas implementadas.

## Fora de escopo

- Supported por intenção
- CI como prova suficiente

## Implementação

- [ ] Estados: Target → Implemented → Harness-verified → Hardware-qualified, ou Blocked.
- [ ] Matriz API × bitness × Native/Bridge/Synthetic.
- [ ] Registrar Acquire/Normalize/Execute/Compose e Color/Depth/Motion/HDR/NR/MFG.
- [ ] Linkar scenario/comando/log.
- [ ] MFG independente de NR.
- [ ] Rodar checker em todo source/test/tool/build/installer first-party handwritten.
- [ ] Testes grandes são repartidos em source files do mesmo executable sempre que isso evita multiplicar jobs.
- [ ] Modularizar CMake/build/installer sem alterar quantidade de validações pesadas.
- [ ] Arquivo a aposentar precisa estar removido antes da RC.

## Revisão obrigatória

- [ ] Implemented != Acquire real.
- [ ] x86/x64 separados.
- [ ] Nenhum “works” sem evidência.
- [ ] Allowlist só generated/vendor/fixtures verificáveis.
- [ ] Zero first-party violation é requisito da RC.
- [ ] Cumprir LOC não aumentou CI caro sem benefício.

## Validação rápida

- [ ] Suite comum por frontend.
- [ ] Amostragem jogos reais.
- [ ] LOC checker.
- [ ] Reexecutar após mudança estrutural.

## Gate

- [ ] Cada rota tem estado/evidência/limite.
- [ ] **Zero first-party handwritten code file >300 linhas.**
- [ ] Claims públicas derivam da matriz.

## Próxima fase

Fase 23.
