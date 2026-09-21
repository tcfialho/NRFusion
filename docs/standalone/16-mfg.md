# Fase 16 — MFG standalone

## Objetivo

Preservar MFG útil sem host genérico do OptiScaler.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- Bloquear NR se MFG não existe
- String parsing por SetOptions

## Checklist de implementação

- [ ] Extrair DlssgTransfusion.
- [ ] Game Controlled/2X/3X/4X/Dynamic.
- [ ] Performance/Enhanced.
- [ ] UI Recomposition.
- [ ] 5X/6X experimental onde suportado.
- [ ] Safe Transition.
- [ ] Late module load.
- [ ] Enums/atomics no hot path.
- [ ] Remover mutex só com análise.
- [ ] PE/fatbin scan apenas load.
- [ ] Fake Streamline client.

## Revisão obrigatória

- [ ] SetOptions per-frame cost.
- [ ] GetState publication.
- [ ] Dynamic/rapid changes.
- [ ] Streamline versions.
- [ ] MFG capability independente de NR.

## Validação rápida

- [ ] Fake client GetState e 2X->4X->2X/Dynamic/late load.
- [ ] Benchmark ProcessSetOptions.
- [ ] Module/version failure.

## Gate de conclusão

- [ ] Sem string/allocation hot path.
- [ ] Safe Transition preservada.
- [ ] NR não depende de MFG.

## Entregáveis

- MFG host layer
- Fake client
- MFG perf report

## Próxima fase

Fase 17.
