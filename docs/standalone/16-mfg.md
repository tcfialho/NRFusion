# Fase 16 — MFG standalone

## Objetivo

Preservar MFG como subsistema independente e distinguir override de MFG nativo de MFG em jogo sem DLSSG.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- Fazer disponibilidade de MFG bloquear NR
- Prometer MFG universal antes de provar uma rota de apresentação
- Parsing de strings por SetOptions

## Implementação

- [ ] Rota A: extrair `DlssgTransfusion` para jogos que já carregam Streamline/DLSSG.
- [ ] Preservar Game Controlled, 2X/3X/4X/Dynamic, Performance/Enhanced, UI Recomposition e Safe Transition.
- [ ] Preservar 5X/6X apenas como experimental onde já suportado.
- [ ] Converter config para enums/atomics fora do hot path; scan/patch apenas no load/reload.
- [ ] Rota B: para jogos sem DLSSG/Streamline, fazer primeiro proof-of-route de apresentação/frame-generation por API antes de escrever integração grande.
- [ ] Não reutilizar o status da Rota A como prova da Rota B.
- [ ] Criar Fake Streamline/DLSSG client para a Rota A.
- [ ] Capability MFG é independente por API/rota e separada de NR.

## Revisão obrigatória

- [ ] SetOptions per-frame é allocation-free e sem string work.
- [ ] Mutex só permanece se concorrência real exigir.
- [ ] GetState publica requested/effective/capability corretamente.
- [ ] Safe Transition continua anti-TDR.
- [ ] Jogo sem DLSSG pode ter NR mesmo quando MFG fica Blocked.

## Validação rápida

- [ ] Fake client: GetState, 2X→4X→2X, Dynamic, late load e failure.
- [ ] Benchmark ProcessSetOptions em massa.
- [ ] Proof-of-route separado para qualquer MFG sintético.
- [ ] Hardware real apenas para patch/runtime/pacing final.

## Gate

- [ ] Rota A preservada sem overhead genérico.
- [ ] Rota B tem evidência técnica ou estado Blocked explícito.
- [ ] MFG possui matriz de qualificação própria.

## Próxima fase

Fase 17.
