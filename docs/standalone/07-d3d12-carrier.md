# Fase 07 — D3D12 x64 carrier

## Objetivo

Qualificar Acquire→Normalize→Execute→Compose sem deixar `SyntheticDx12Provider.cpp` continuar monolítico.

## Dependências

Fases 04–06.

## Fora de escopo

- Outras APIs
- Universalizar MFG

## Implementação

- [ ] Reusar provider/testes atuais, mas dividir provider oversized antes de mudança substancial.
- [ ] Separar acquisition, resource pool/slots, dispatch/interop e compose quando responsabilidades exigirem.
- [ ] Definir Acquire seam e lifetime de color/depth/motion/exposure.
- [ ] Usar contrato DLSS/RR existente quando confiável; senão Synthetic FrameContract honesto.
- [ ] Integrar registry/NrSession e preservar caller state.
- [ ] Resize/device removal/guides ausentes.
- [ ] Disabled quase pass-through.
- [ ] Cada arquivo de carrier <=300 linhas.

## Revisão obrigatória

- [ ] Provider aceitar ResourceRef não prova Acquire.
- [ ] Nenhum CPU pixel path.
- [ ] `EnsureSlotResources` só cria em init/reconfigure/resolution change.
- [ ] Locks/descriptor writes/copies entram na auditoria.
- [ ] Split segue resource ownership e não sequência textual do arquivo antigo.

## Validação rápida

- [ ] Testes atuais antes/depois do split.
- [ ] Harness Acquire controlado/lifecycle.
- [ ] Checker de LOC.
- [ ] Jogos reais só para Acquire/model final.

## Gate

- [ ] Acquire/Normalize/Execute/Compose têm owner.
- [ ] Fast path cumpre steady-state contract.
- [ ] D3D12 carrier inteiro respeita <=300 por arquivo.

## Próxima fase

Fase 08.
