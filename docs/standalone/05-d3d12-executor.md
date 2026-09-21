# Fase 05 — Executor DLSS 5 D3D12 canônico

## Objetivo

Extrair o executor D3D12 maduro para ownership próprio preservando sua semântica.

## Dependências

Fase 04.

## Fora de escopo

- Otimizar barriers/copies
- Trocar algoritmo visual
- Redesenhar resource model

## Implementação

- [ ] Definir input/output explícitos do executor.
- [ ] Substituir OptiScaler Config/State por parâmetros/snapshot.
- [ ] Preservar feature pending-submission e rebuild.
- [ ] Preservar subrect/padding, depth/motion compatibility e resource states.
- [ ] Preservar pre/post-SR, RR, history e resets.
- [ ] Preservar scale <100/>100, multipass, HDR/exposure e residual.
- [ ] Preservar failure latches, precision rebuild e lazy allocation.

## Revisão obrigatória

- [ ] Para cada resource: owner, create trigger, states, release, resize, failure.
- [ ] Para cada barrier: estado anterior/próximo e caller guarantee.
- [ ] Early return não deixa state/lifetime diferente.
- [ ] Workaround sem repro continua até fase de otimização.

## Validação rápida

- [ ] Compilar/exercitar boundary com fake executor/harness onde possível.
- [ ] Loop de reset/resize/rebuild usando substitutos controlados.
- [ ] Executar modelo real somente na qualificação com hardware compatível.

## Gate

- [ ] Sem dependência direta de OptiScaler.
- [ ] Extração revisável por diff e resource-state map.
- [ ] Nenhuma otimização funcional misturada.

## Próxima fase

Fase 06.
