# Fase 07 — D3D12 x64 carrier

## Status

**EM ANDAMENTO — auditoria inicial concluída; split mecânico ainda bloqueado por compatibilidade legada.**

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


## Auditoria inicial — 2026-09-22

Estado atual:
- `include/nrfusion/SyntheticDx12Provider.hpp`: 103 linhas;
- `src/SyntheticDx12Provider.cpp`: 596 linhas, ainda grandfathered e read-only;
- testes D3D12 existentes: `nrfusion_synthetic_dx12_test`,
  `nrfusion_synthetic_dx12_scale_gate_test` e harness D3D12;
- boundaries do monólito confirmados: initialization/shaders, slot resources,
  submit/extract e compose/poll.

O split direto do provider não é seguro enquanto o fallback OptiScaler estiver ativo:
`tools/apply_to_optiscaler.py` possui 3900 linhas e copia nominalmente apenas
`SyntheticDx12Provider.cpp`. Dividir o source sem atualizar esse manifest quebraria o fallback;
tocar o patcher agora também violaria a regra estrutural, porque ele é um arquivo handwritten >300
e a Fase 23 manda removê-lo, não refatorá-lo prematuramente.

Portanto nenhum source D3D12 foi alterado neste subgate. O blocker é de integração/ownership, não de
implementação do carrier.

## Próxima ação exata

Definir uma migração que permita ao standalone sair do monólito sem quebrar o fallback legado e sem
modificar o patcher >300. Só depois executar o split mecânico e comparar os testes atuais antes/depois.
