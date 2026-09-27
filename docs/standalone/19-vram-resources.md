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


## Subgate 19a — retirement de scratch opcional

Auditoria:
- `D3D12NrScratchResources` já possuía `Retire(kind)`, mas o executor só fazia
  `EnsureOptional()` quando a feature estava ativa.
- Ao voltar multipass/reduced/crop/residual para off, os recursos opcionais continuavam
  residentes até resize completo ou shutdown.
- `PassScratch` também era alocado por `requestedPasses > 1` mesmo quando
  `PreparePassFeatures()` reduzia o número efetivo de passes para 1.

Correção:
- `D3D12NrScratchUsage` descreve apenas necessidade de scratch, sem virar manager global.
- `RetireUnused()` faz preflight da capacidade da retirement queue e aposenta de forma
  fail-closed os opcionais inativos.
- `PassScratch` agora segue `effectivePasses > 1`.
- `ColorSmall` segue reduced; `ActiveColor` segue crop; os quatro recursos residual
  seguem `acrossRr`.
- `OutputNative` não é requerido pelo executor atual e qualquer instância stale é aposentada.
- Retirement continua deferred; nenhum recurso em voo é liberado diretamente.

Validação:
- `nrfusion_nr_scratch_resources_tests` cobre retire por usage e contagem da retirement queue.
- O teste foi incluído no Windows fast gate, não apenas compilado.
- Portable `36324845068`: PASS.
- Focused Portable `36324845106`: PASS.
- Windows `36324845150`: PASS.
- Checkpoint `nrfusion-source-fe3a595db19b7f2fd57326715089ebbffa6eefac`,
  artifact `10933437248`,
  sha256 `81bc41d6ea7ad43586fea26b91db1072e2445267c0ddb889a78a0af637d8628e`.
- Código validado: `fe3a595db19b7f2fd57326715089ebbffa6eefac`.

**Subgate 19a CLOSED. Fase 19 permanece IN PROGRESS.**

Próxima ação: adicionar `resourceCount` e `logicalBytes` do scratch em owner separado.
`D3D12NrScratchResources.cpp` está em 298 linhas e não deve crescer além de 300.
A métrica deve ser control-plane only, sem device query, allocation, mutex ou GPU work.
