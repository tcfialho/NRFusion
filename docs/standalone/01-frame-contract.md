# Fase 01 — Universal FrameContract

## Status

**Concluída.** Evidência: [01-frame-contract-evidence.md](01-frame-contract-evidence.md).

## Objetivo

Definir contrato API-independente, explícito e pequeno sem aumentar o monólito `Types.hpp`.

## Implementação

- [x] Extrair o domínio de frame para `FrameContract.hpp`.
- [x] Definir color obrigatório e depth/motion/exposure/reactive como opcionais.
- [x] Representar provenance, reliability, ownership e lifetime sem tipos D3D/Vulkan/OpenGL.
- [x] Vincular recursos explicitamente a `sourceFrameId` quando o provider conhece essa identidade.
- [x] Separar camera cut de `resetHistory`.
- [x] Adicionar host token, view e configuration generation à identidade do frame.
- [x] Fazer metadata explícita prevalecer sobre flags legados de reliability/motion.
- [x] Rejeitar metadata parcialmente preenchida e resource de outro frame.
- [x] Manter compatibilidade temporária com providers hospedados no OptiScaler.
- [x] Manter capability factual separada de preferência/policy.

## Revisão obrigatória

- [x] Resource válido não implica reliable.
- [x] Provenance explícita não pode ser mascarada por flag legado.
- [x] Frame N rejeita resource explicitamente ligado a N+1.
- [x] Core contract não contém ponteiro/API gráfico.
- [x] `ResourceRef` e `FrameContext` permanecem trivially-copyable e standard-layout.
- [x] Arquivos novos/tocados ficam <=300 linhas.

## Validação rápida

- [x] Fake provider válido/incompleto/contraditório.
- [x] 100.000 transições determinísticas de frame/provenance sem GPU.
- [x] Compilação com `-Wall -Wextra -Wpedantic -Werror`.
- [x] `PipelinePolicy` compilado consumindo reliability pelo novo contrato.
- [x] Compatibilidade de agregados legados exercitada.
- [x] IPC x86 auditado quanto a IDs fixos e ausência de tipos gráficos no contrato.

## Gate

- [x] Core consome reliability/provenance sem casts de API.
- [x] Contrato permite providers descreverem limites sem fingir Native/Reliable.
- [x] Contrato está separado por domínio e abaixo de 300 linhas.

## Próxima fase

Fase 02 — Standalone runtime shell.
