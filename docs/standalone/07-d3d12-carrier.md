# Fase 07 — D3D12 x64 carrier

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose e decompor o provider atual por ownership real.

## Dependências

Fases 04–06.

## Fora de escopo

- Outras APIs
- Universalizar MFG

## Implementação

- [ ] Reusar provider/testes atuais, com split mecânico antes de evolução.
- [ ] Boundary sugerida pelo código atual: **initialization/shaders**, **slot resources**, **frame submit/extract**, **compose/poll**.
- [ ] Definir Acquire seam/lifetime de color/depth/motion/exposure.
- [ ] Usar contrato DLSS/RR confiável ou Synthetic FrameContract honesto.
- [ ] Integrar registry/NrSession.
- [ ] Resize/device removal/guides ausentes.
- [ ] Disabled quase pass-through.
- [ ] Cada arquivo <=300 linhas.

## Revisão obrigatória

- [ ] Provider aceitar ResourceRef não prova Acquire.
- [ ] Nenhum CPU pixel path.
- [ ] `EnsureSlotResources` só cria em init/reconfigure/resolution change.
- [ ] Descriptor writes/copies/locks são contabilizados.
- [ ] Split segue resource ownership, não ordem textual.

## Validação rápida

- [ ] Testes atuais antes/depois do split.
- [ ] Harness Acquire controlado.
- [ ] LOC checker.
- [ ] Jogos reais só para Acquire/model final.

## Gate

- [ ] Quatro etapas têm owner.
- [ ] Fast path cumpre steady-state.
- [ ] D3D12 carrier <=300 por arquivo.

## Próxima fase

Fase 08.
