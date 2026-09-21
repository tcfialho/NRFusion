# Fase 05 — Executor DLSS 5 D3D12 canônico

## Objetivo

Extrair o executor maduro atual sem mudar semântica.

## Dependências

Fase 04.

## Fora de escopo

- Otimizar barriers
- Redesenhar recursos
- Trocar algoritmo visual

## Checklist de implementação

- [ ] Input/output explícitos.
- [ ] Remover Config/State do OptiScaler.
- [ ] Preservar pending-submission.
- [ ] Preservar subrect/padding.
- [ ] Preservar depth/motion compatibility.
- [ ] Preservar pre/post-SR/RR/history.
- [ ] Preservar scale<100/>100, multipass, HDR/exposure/residual.
- [ ] Preservar failure latches/precision rebuild/lazy allocation.

## Revisão obrigatória

- [ ] Para cada resource: owner/create/state/release/resize/failure.
- [ ] Para cada barrier: estados conhecidos.
- [ ] Early returns preservam cleanup.
- [ ] Nenhum workaround removido sem evidência.

## Validação rápida

- [ ] MiniGame D3D12 before/after.
- [ ] Create/evaluate failure.
- [ ] Resize/reset/rebuild loops.

## Gate de conclusão

- [ ] Equivalência no harness.
- [ ] Sem dependência direta do OptiScaler.
- [ ] Sem otimização misturada.

## Entregáveis

- NrExecutorD3D12
- Resource/state map

## Próxima fase

Fase 06.
