# Fase 01 — Universal FrameContract

## Objetivo

Formalizar o contrato API-independente consumido pelo core.

## Dependências

Fase 00.

## Fora de escopo

- Executar DLSS 5
- Interop específico

## Checklist de implementação

- [ ] Auditar FrameContext/ResourceRef.
- [ ] Definir mandatory/optional resources.
- [ ] Codificar provenance e confiabilidade de depth/motion.
- [ ] Definir color/exposure/HDR/jitter/reset.
- [ ] Definir render/output resolution.
- [ ] Definir frame/work identity assíncrona.
- [ ] Definir lifetime API-neutral.
- [ ] Separar capability fact de policy preference.

## Revisão obrigatória

- [ ] Resource presente não implica reliability.
- [ ] Synthetic nunca finge Native.
- [ ] Evitar frame N/N+1 e stale resources.
- [ ] Core não recebe ID3D/Vk/GL types.

## Validação rápida

- [ ] Fake providers válidos/inválidos.
- [ ] Milhões de transições de provenance sem GPU.
- [ ] Revisar dados necessários no IPC x86.

## Gate de conclusão

- [ ] Todos os providers podem representar seus dados honestamente.
- [ ] Contratos ambíguos falham fechados.

## Entregáveis

- Contrato revisado
- Tabela provenance/reliability

## Próxima fase

Fase 02.
