# Fase 01 — Universal FrameContract

## Objetivo

Definir contrato API-independente, explícito e pequeno o bastante para não virar um novo `Types.hpp` monolítico.

## Dependências

Fase 00.

## Fora de escopo

- Executar NR
- Interop específico

## Implementação

- [ ] Auditar FrameContext/ResourceRef existentes.
- [ ] Definir mandatory/optional resources, dimensões e provenance.
- [ ] Representar reliability de depth/motion/exposure.
- [ ] Definir jitter, HDR, camera-cut/reset e frame/work identity.
- [ ] Definir lifetime/ownership sem ID3D/Vk/GL no core.
- [ ] Separar capability factual de policy preference.
- [ ] Se `Types.hpp` precisar crescer, extrair contratos por domínio antes de adicionar responsabilidade.

## Revisão obrigatória

- [ ] Resource válido não implica reliable.
- [ ] Synthetic nunca finge Native.
- [ ] Frame N não consome recurso/timing de N+1.
- [ ] Headers novos/tocados ficam <=300 linhas; API pública não acumula implementação.

## Validação rápida

- [ ] Fake providers válidos/incompletos/contraditórios.
- [ ] Stress de provenance/reset sem GPU.
- [ ] Confirmar uso pelo IPC x86.

## Gate

- [ ] Core consome contrato sem casts de API.
- [ ] Providers representam limites honestamente.
- [ ] Contrato está dividido por domínio antes de qualquer arquivo atingir 300 linhas.

## Próxima fase

Fase 02.
