# Fase 16 — MFG standalone

## Objetivo

Preservar MFG independente de NR sem levar `DlssgTransfusion.cpp` como monólito.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- MFG bloquear NR
- Prometer MFG universal sem rota de apresentação

## Implementação

- [ ] Rota A: jogos com Streamline/DLSSG; extrair comportamento atual.
- [ ] Antes de expandir Transfusion oversized, dividir module discovery/patching, option state, transitions e state publication.
- [ ] Preservar Game Controlled, 2X/3X/4X/Dynamic, Quality, UI Recomposition e Safe Transition.
- [ ] 5X/6X continuam experimentais quando suportados.
- [ ] Config vira enums/atomics fora do hot path; scans só load/reload.
- [ ] Rota B sem DLSSG exige proof-of-route separado por API.
- [ ] Fake Streamline client fica em arquivos <=300.
- [ ] Capability MFG independente de NR.

## Revisão obrigatória

- [ ] SetOptions allocation-free/sem string work.
- [ ] Mutex só com concorrência demonstrada.
- [ ] Patch code separado do per-frame state.
- [ ] GetState não mente capability.
- [ ] Split segue lifecycle do módulo, não trechos arbitrários.

## Validação rápida

- [ ] Fake client 2X→4X→2X/Dynamic/late load/failure.
- [ ] Benchmark ProcessSetOptions.
- [ ] Proof separado para MFG sintético.
- [ ] LOC checker.

## Gate

- [ ] Rota A preservada.
- [ ] Rota B provada ou Blocked.
- [ ] MFG inteiro tocado <=300 linhas por arquivo.

## Próxima fase

Fase 17.
