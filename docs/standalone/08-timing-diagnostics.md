# Fase 08 — Timing e Diagnostics desacoplados

## Objetivo

Manter timing do controller reduzindo instrumentação always-on.

## Dependências

Fase 07.

## Fora de escopo

- Remover diagnóstico
- Introduzir query wait síncrono

## Checklist de implementação

- [ ] Query heap/ring compartilhado.
- [ ] Normal: 2 timestamps + 1 ResolveQueryData por amostra.
- [ ] Cache timestamp frequency.
- [ ] WorkId exato.
- [ ] Sem timer object/frame.
- [ ] Model/resolve timing só Diagnostics.
- [ ] Readback persistente/reutilizado.

## Revisão obrigatória

- [ ] Contar GPU commands before/after.
- [ ] Evitar slot reuse prematuro.
- [ ] Diagnostics off sem recursos exclusivos.
- [ ] Detailed timing não muda policy.

## Validação rápida

- [ ] Harness conta EndQuery/Resolve.
- [ ] Benchmark Diagnostics off/on.
- [ ] Delayed retirement.

## Gate de conclusão

- [ ] Normal path reduzido e sem wait.
- [ ] Stale timing rejeitado.
- [ ] Off não paga On.

## Entregáveis

- Timing ring
- Diagnostics gate
- Command-count report

## Próxima fase

Fase 09.
