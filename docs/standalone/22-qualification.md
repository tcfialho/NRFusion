# Fase 22 — Matriz de qualificação por API/bitness

## Objetivo

Dar um estado inequívoco e evidência reproduzível a cada rota.

## Dependências

Rotas candidatas implementadas.

## Fora de escopo

- Supported por intenção
- Compile/CI como prova suficiente

## Implementação

- [ ] Usar estados: Target → Implemented → Harness-verified → Hardware-qualified, ou Blocked.
- [ ] Matriz API × bitness × Native/Bridge/Synthetic.
- [ ] Registrar Color/Depth/Motion/HDR/NR/MFG/GPU-only transport separadamente.
- [ ] Linkar cenário/comando do harness usado como evidência.
- [ ] Exigir acquisition, sync, ownership, compose, resize e failure path.
- [ ] Registrar MFG separadamente de NR.
- [ ] Manter blocker técnico específico para rotas não qualificadas.

## Revisão obrigatória

- [ ] x86/x64 têm gates distintos quando arquitetura difere.
- [ ] NR universal não implica MFG universal.
- [ ] Uma API pode ter Synthetic qualified e Native não aplicável.
- [ ] Nenhum 'works' sem nível de evidência.

## Validação rápida

- [ ] Executar scenario suite comum por frontend.
- [ ] Depois amostragem em jogos reais por API/engine.
- [ ] Reexecutar matriz após mudança estrutural de carrier/executor.

## Gate

- [ ] Cada célula tem estado, evidência e limitações.
- [ ] Claims públicas podem ser derivadas diretamente da matriz.

## Próxima fase

Fase 23.
