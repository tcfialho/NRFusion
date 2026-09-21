# Fase 08 — Timing e Diagnostics desacoplados

## Objetivo

Manter timing suficiente para o controller e tornar detalhamento custo sob demanda.

## Dependências

Fase 07.

## Fora de escopo

- Remover capacidade de diagnóstico
- Adicionar query wait síncrono

## Implementação

- [ ] Usar query heap/ring compartilhado e bounded.
- [ ] Normal path: um intervalo total; alvo de 2 timestamps + 1 ResolveQueryData por amostra.
- [ ] Cachear timestamp frequency enquanto queue for a mesma.
- [ ] Associar resultado ao WorkId/generation exatos.
- [ ] Readback persistente ou ring reutilizado.
- [ ] Model/resolve timing só quando Diagnostics exigir.
- [ ] FakeTimingSource usa a mesma interface do real.

## Revisão obrigatória

- [ ] Slot não pode ser reutilizado antes do retirement.
- [ ] Stale timing nunca é reaplicado.
- [ ] Diagnostics off não mantém resources/comandos exclusivos.
- [ ] Detailed timing não muda policy.

## Validação rápida

- [ ] Harness conta comandos/counters com fake/real D3D12 quando disponível.
- [ ] Delayed retirement e dropped timing.
- [ ] Comparar Diagnostics off/on.

## Gate

- [ ] Normal path sem wait e bounded.
- [ ] Off não paga On.
- [ ] Controller recebe apenas amostra fresca corretamente associada.

## Próxima fase

Fase 09.
