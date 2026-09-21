# Fase 04 — NGX feature identity registry

## Objetivo

Distinguir SR, RR, FG e unknown estruturalmente.

## Dependências

Fase 03.

## Fora de escopo

- Executar NR
- Inferir tipo por depth+motion

## Checklist de implementação

- [ ] Interceptar create/release.
- [ ] Registrar tipo/handle/context/viewport necessário.
- [ ] Não registrar failed create.
- [ ] Tratar handle reuse/recreation.
- [ ] Unknown passa intacto.
- [ ] FG nunca dispara NR.

## Revisão obrigatória

- [ ] SR+FG e RR+FG.
- [ ] Múltiplos viewports.
- [ ] Late release/reuse.
- [ ] Versões desconhecidas.

## Validação rápida

- [ ] Fake client com milhares de create/release/reuse.
- [ ] Failure injection.
- [ ] Assert Evaluate(FG) nunca chama NR.

## Gate de conclusão

- [ ] Impossível confundir FG com SR/RR.
- [ ] Registry sem allocation em Evaluate steady.

## Entregáveis

- Feature registry
- Regression scenarios

## Próxima fase

Fase 05.
