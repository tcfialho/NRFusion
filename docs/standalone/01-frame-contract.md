# Fase 01 — Universal FrameContract

## Objetivo

Definir um contrato de frame explícito, API-independente e honesto sobre provenance.

## Dependências

Fase 00.

## Fora de escopo

- Executar NR
- Interop específico de API

## Implementação

- [ ] Auditar FrameContext/ResourceRef existentes.
- [ ] Definir mandatory/optional resources e dimensões válidas.
- [ ] Representar provenance e reliability de depth/motion/exposure.
- [ ] Definir jitter, HDR, camera-cut/reset e frame/work identity.
- [ ] Definir lifetime/ownership sem expor tipos ID3D/Vk/GL ao core.
- [ ] Separar capability factual de preferência/policy.
- [ ] Definir quando mudança de provenance invalida history.

## Revisão obrigatória

- [ ] Resource válido não implica reliable.
- [ ] Synthetic nunca marca dado como Native por conveniência.
- [ ] Frame N não pode consumir recurso/timing de N+1.
- [ ] Ambiguidade de ownership ou provenance falha fechada.

## Validação rápida

- [ ] Fake providers válidos, incompletos e contraditórios.
- [ ] Stress de mudanças de provenance/reset sem GPU.
- [ ] Confirmar que o mesmo contrato serve ao IPC x86.

## Gate

- [ ] Core consome contrato sem casts de API.
- [ ] Todos os providers conseguem representar limites/fallbacks sem mentir.

## Próxima fase

Fase 02.
