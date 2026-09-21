# Fase 19 — Disciplina de recursos e VRAM

## Objetivo

Garantir lazy allocation e resource ownership explícito sem criar um ResourceManager gigante.

## Dependências

Contínua; consolidar após features principais.

## Fora de escopo

- Prometer economia sem medir
- Alocar preventivamente

## Implementação

- [ ] Manter resource ledger: size formula, owner, trigger, reuse, release e resize.
- [ ] Multipass/residual/hold/diagnostics off não mantêm extras.
- [ ] Evitar duplicação carrier/executor.
- [ ] Separar persistent/transient/vendor-owned.
- [ ] Liberar gerações antigas após retirement.
- [ ] Counters de bytes/resources onde controlamos allocation.
- [ ] Ledger/pool por domínio; nenhum `ResourceManager` multifunção >300 linhas.

## Revisão obrigatória

- [ ] Resize/toggle/failure não acumulam generations.
- [ ] Shared resources contam no orçamento.
- [ ] Não trocar VRAM por CPU sem tradeoff.
- [ ] Ownership continua local ao subsistema que usa o recurso.

## Validação rápida

- [ ] Resize/toggle/failure loops.
- [ ] Resource counts por feature.
- [ ] Hardware real mede peak final.
- [ ] LOC checker.

## Gate

- [ ] Sem leak/recurso inativo.
- [ ] VRAM alvo <= baseline equivalente.
- [ ] Código de resource ownership <=300 por arquivo.

## Próxima fase

Fase 20.
