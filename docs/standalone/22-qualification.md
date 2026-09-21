# Fase 22 — Matriz de qualificação por API/bitness

## Objetivo

Dar estado inequívoco e evidência reproduzível a cada rota completa.

## Dependências

Rotas candidatas implementadas.

## Fora de escopo

- Supported por intenção
- Compile/CI como prova suficiente

## Implementação

- [ ] Estados: Target → Implemented → Harness-verified → Hardware-qualified, ou Blocked.
- [ ] Matriz API × bitness × Native/Bridge/Synthetic.
- [ ] Para cada célula registrar Acquire, Normalize, Execute e Compose separadamente.
- [ ] Registrar Color/Depth/Motion/HDR/NR/MFG/GPU-only transport.
- [ ] Linkar scenario/comando/log do harness usado como evidência.
- [ ] MFG recebe estado independente de NR.
- [ ] Blocker técnico é específico e reabrível.

## Revisão obrigatória

- [ ] Implemented não implica que Acquire funciona em engine real.
- [ ] x86/x64 têm gates distintos quando arquitetura difere.
- [ ] Synthetic qualified não implica Native.
- [ ] Nenhum “works” sem nível de evidência.

## Validação rápida

- [ ] Suite comum por frontend.
- [ ] Depois amostragem em jogos reais por API/engine.
- [ ] Reexecutar evidência após mudança estrutural.

## Gate

- [ ] Cada célula tem estado, evidência, limitações e próxima prova necessária.
- [ ] Claims públicas podem ser derivadas diretamente da matriz.

## Próxima fase

Fase 23.
