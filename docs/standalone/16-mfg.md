# Fase 16 — MFG standalone

## Objetivo

Preservar MFG como subsistema independente, sem fazer sua disponibilidade limitar NR.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- Prometer MFG universal antes de qualificar cada rota
- Parsing de strings por SetOptions

## Implementação

- [ ] Extrair DlssgTransfusion das dependências de OptiScaler.
- [ ] Preservar Game Controlled, 2X/3X/4X/Dynamic, Performance/Enhanced e UI Recomposition.
- [ ] Preservar 5X/6X apenas como experimental onde já suportado.
- [ ] Preservar Safe Transition e late nvngx_dlssg.dll load.
- [ ] Converter config para enums/atomics fora do hot path.
- [ ] PE/fatbin scan somente em load/reload.
- [ ] Criar Fake Streamline/DLSSG client no harness.
- [ ] Definir capability MFG separada por rota/API.

## Revisão obrigatória

- [ ] SetOptions pode ser per-frame: caminho deve ser allocation-free e sem string work.
- [ ] Mutex só permanece se concorrência real exigir.
- [ ] GetState publica requested/effective sem mentir sobre capability.
- [ ] NR continua funcional quando MFG é Blocked.

## Validação rápida

- [ ] Fake client: GetState, 2X→4X→2X, Dynamic, late load, failure.
- [ ] Benchmark ProcessSetOptions em massa.
- [ ] Hardware real somente para patch/runtime/pace final.

## Gate

- [ ] Hot path sem alocação/string scan.
- [ ] Safe Transition preservada.
- [ ] MFG tem matriz de qualificação própria.

## Próxima fase

Fase 17.
