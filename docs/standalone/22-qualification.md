# Fase 22 — Matriz de qualificação por API/bitness

## Objetivo

Dar evidência reproduzível por rota e fechar toda dívida de arquivos >300 antes da release candidate.

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
- [ ] Rodar checker de 300 linhas em todo first-party handwritten source/test/tool/build/installer.
- [ ] Splitar testes grandes por subsystem/scenario.
- [ ] Modularizar CMake/build/installer se qualquer handwritten file exceder 300.
- [ ] Arquivo marcado para retirement precisa estar removido antes de declarar RC.

## Revisão obrigatória

- [ ] Implemented != Acquire real.
- [ ] x86/x64 separados.
- [ ] Nenhum “works” sem evidência.
- [ ] Allowlist LOC contém só generated/vendor/fixtures verificáveis.
- [ ] Zero first-party handwritten violation é requisito da RC.

## Validação rápida

- [ ] Suite comum por frontend.
- [ ] Amostragem jogos reais.
- [ ] LOC checker do repositório.
- [ ] Reexecutar após mudança estrutural.

## Gate

- [ ] Cada rota tem estado/evidência/limite.
- [ ] **Zero first-party handwritten code file >300 linhas.**
- [ ] Claims públicas derivam da matriz.

## Próxima fase

Fase 23.
