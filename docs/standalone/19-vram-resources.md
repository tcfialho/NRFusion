# Fase 19 — Disciplina de recursos e VRAM

## Objetivo

Lazy allocation e VRAM <= baseline equivalente.

## Dependências

Contínua; consolidar após features.

## Fora de escopo

- Prometer economia sem medir
- Alocar preventivamente

## Checklist de implementação

- [ ] Inventariar resources e size formula.
- [ ] Activation/reuse/release/resize.
- [ ] Multipass=1 sem extras.
- [ ] Residual off sem histories.
- [ ] Hold off sem heldColor.
- [ ] Diagnostics off sem resources exclusivos.
- [ ] Evitar duplication carrier/executor.
- [ ] Reclaim ao desativar quando seguro.
- [ ] Separar persistent/transient peak.

## Revisão obrigatória

- [ ] Owner/lifetime explícito.
- [ ] Resize sem acumular gerações.
- [ ] Failure libera criado.
- [ ] Contabilizar carrier+executor.

## Validação rápida

- [ ] Harness alterna features/resolution.
- [ ] Resource counts.
- [ ] Real hardware VRAM no qualification.

## Gate de conclusão

- [ ] Sem leak.
- [ ] Inactive não retém recurso.
- [ ] Meta <= baseline.

## Entregáveis

- Resource ledger
- VRAM budget

## Próxima fase

Fase 20.
