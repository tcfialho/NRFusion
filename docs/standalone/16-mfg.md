# Fase 16 — MFG standalone

## Objetivo

Preservar MFG independente de NR decompondo patching e hot state.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- MFG bloquear NR
- MFG universal sem rota comprovada

## Implementação

- [ ] Rota A: jogos com Streamline/DLSSG.
- [ ] Split `DlssgTransfusion.cpp`: **PE/fatbin/LZ4 parsing**, **module patch application**, **runtime option/state**, **safe transitions/publication**.
- [ ] Preservar Game Controlled, multipliers, Quality, UI Recomposition e Safe Transition.
- [ ] 5X/6X continuam experimentais quando suportados.
- [ ] Config vira enums/atomics fora do hot path; scans só load/reload.
- [ ] Rota B sem DLSSG exige proof separado.
- [ ] Fake Streamline client <=300 por arquivo.
- [ ] Capability MFG independente de NR.

## Revisão obrigatória

- [ ] SetOptions allocation-free/sem string work.
- [ ] Patch/parser nunca roda per-frame.
- [ ] Mutex só com concorrência real.
- [ ] GetState não mente capability.
- [ ] Split segue lifecycle module/runtime.

## Validação rápida

- [ ] Fake client rapid changes/late load/failure.
- [ ] Benchmark ProcessSetOptions.
- [ ] Proof separado MFG sintético.
- [ ] LOC checker.

## Gate

- [ ] Rota A preservada.
- [ ] Rota B provada ou Blocked.
- [ ] MFG <=300 por arquivo.

## Próxima fase

Fase 17.
