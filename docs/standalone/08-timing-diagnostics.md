# Fase 08 — Timing e Diagnostics desacoplados

## Objetivo

Manter timing suficiente ao controller com módulos pequenos e custo detalhado só sob demanda.

## Dependências

Fase 07.

## Fora de escopo

- Remover diagnóstico
- Copiar waits bloqueantes do harness para o produto

## Implementação

- [ ] Query ring, readback e associação WorkId ficam em componentes focados.
- [ ] Normal path: um intervalo total; alvo 2 timestamps + 1 resolve por amostra.
- [ ] Consumir resultado aposentado sem esperar GPU atual.
- [ ] Cachear timestamp frequency por queue.
- [ ] Model/resolve timing só em Diagnostics.
- [ ] FakeTimingSource usa a mesma interface.
- [ ] Se timing + diagnostics se aproximarem de 250 linhas, separar coleta de apresentação/status.

## Revisão obrigatória

- [ ] Slot não reutiliza antes do retirement.
- [ ] Stale timing não reaplica.
- [ ] Diagnostics off não mantém recursos/comandos exclusivos.
- [ ] Map/readback não introduz sync implícita.
- [ ] Nenhum arquivo <=300 é mantido artificialmente por código comprimido.

## Validação rápida

- [ ] Fake timing valida association/staleness.
- [ ] Correctness harness valida queries reais.
- [ ] Benchmark exclui waits artificiais.
- [ ] Checker de LOC.

## Gate

- [ ] Produto normal não espera GPU por telemetry.
- [ ] Off não paga On.
- [ ] Timing/diagnostics <=300 linhas por arquivo.

## Próxima fase

Fase 09.
