# Fase 13 — D3D10 carrier

## Objetivo

Provar uma bridge D3D10 viável antes de investir em implementação completa.

## Dependências

Fases 07–08.

## Fora de escopo

- Executor NR próprio D3D10
- CPU screenshot

## Implementação

- [ ] Primeiro produzir proof-of-route para sharing/interoperabilidade no mesmo adapter.
- [ ] Definir capture/compose points mínimos.
- [ ] Capturar color GPU-side; depth/motion só se tecnicamente confiáveis.
- [ ] Bridge para D3D12 canônico.
- [ ] Definir sincronização sem blocking wait recorrente.
- [ ] Tratar resize/device recreation.
- [ ] Somente após proof-of-route, adicionar frontend D3D10 ao runner.

## Revisão obrigatória

- [ ] Cada copy full-frame tem razão e contador.
- [ ] Sem sharing seguro = Blocked, não gambiarra CPU.
- [ ] Provenance de guides não é inferida.
- [ ] Legacy API não pode forçar arquitetura especial no core.

## Validação rápida

- [ ] Micro-harness proof-of-route primeiro.
- [ ] Depois steady/resize com fake executor.
- [ ] Medir copies/sync/CPU.

## Gate

- [ ] Há rota GPU-resident demonstrável ou blocker técnico documentado.
- [ ] Nenhum código grande antes dessa decisão.

## Próxima fase

Fase 14.
