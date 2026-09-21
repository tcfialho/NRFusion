# Fase 19 — Disciplina de recursos e VRAM

## Objetivo

Garantir lazy allocation, reuse previsível e orçamento de VRAM verificável.

## Dependências

Contínua; consolidar após features principais.

## Fora de escopo

- Prometer economia sem medir
- Alocar recursos por conveniência futura

## Implementação

- [ ] Manter resource ledger com size formula, owner, trigger, reuse, release e resize.
- [ ] Multipass=1 não cria extras; residual/hold/diagnostics off não retêm resources exclusivos.
- [ ] Evitar duplicação carrier/executor quando interop permite reuse.
- [ ] Separar persistent, transient peak e vendor/NGX-owned quando observável.
- [ ] Liberar gerações antigas após retirement seguro.
- [ ] Adicionar counters de bytes/resources no harness onde o projeto controla a alocação.

## Revisão obrigatória

- [ ] Resize/toggle/failure não acumulam generations.
- [ ] Shared resources contam no orçamento total.
- [ ] Não trocar VRAM por CPU sem tradeoff explícito.
- [ ] Driver-reported VRAM é qualificação final, não substituto do ledger.

## Validação rápida

- [ ] Loops de resize/toggle/failure no harness.
- [ ] Comparar resource ledger por feature.
- [ ] Hardware real mede peak VRAM no final.

## Gate

- [ ] Sem leaks e sem resource inativo desnecessário.
- [ ] Meta equivalente <= baseline atual.

## Próxima fase

Fase 20.
