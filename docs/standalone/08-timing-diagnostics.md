# Fase 08 — Timing e Diagnostics desacoplados

## Objetivo

Manter timing suficiente para o controller e tornar detalhamento custo sob demanda.

## Dependências

Fase 07.

## Fora de escopo

- Remover capacidade de diagnóstico
- Copiar o modelo bloqueante de timing do harness para o produto

## Implementação

- [ ] Usar query heap/ring compartilhado e bounded.
- [ ] Normal path: um intervalo total; alvo de 2 timestamps + 1 ResolveQueryData por amostra.
- [ ] Consumir resultados aposentados sem esperar a GPU no frame atual.
- [ ] Cachear timestamp frequency enquanto queue for a mesma.
- [ ] Associar resultado ao WorkId/generation exatos.
- [ ] Readback persistente ou ring reutilizado.
- [ ] Model/resolve timing só quando Diagnostics exigir.
- [ ] FakeTimingSource usa a mesma interface do real.

## Revisão obrigatória

- [ ] Slot não é reutilizado antes do retirement.
- [ ] Stale timing nunca é reaplicado.
- [ ] Diagnostics off não mantém resources/comandos exclusivos.
- [ ] Map/Unmap ou readback não introduzem sync implícita.
- [ ] Detailed timing não muda policy.

## Validação rápida

- [ ] FakeTimingSource valida association/staleness sem GPU.
- [ ] Correctness harness valida queries reais, mesmo que use wait.
- [ ] Benchmark do host exclui waits artificiais do harness.
- [ ] Comparar Diagnostics off/on.

## Gate

- [ ] Produto normal não espera a GPU por telemetry.
- [ ] Off não paga On.
- [ ] Controller recebe apenas amostra fresca corretamente associada.

## Próxima fase

Fase 09.
