# Fase 13 — D3D10 carrier

## Objetivo

Provar Acquire e bridge D3D10 viáveis antes de investir em implementação completa.

## Dependências

Fases 07–08.

## Fora de escopo

- Executor NR próprio D3D10
- CPU screenshot

## Implementação

- [ ] Localizar seam de Acquire e ownership de resources D3D10/DXGI.
- [ ] Produzir proof-of-route de compartilhamento no mesmo adapter.
- [ ] Definir color/depth/motion realmente acessíveis.
- [ ] Bridge para D3D12 canônico e compose back.
- [ ] Definir sync sem blocking recorrente.
- [ ] Tratar resize/device recreation.
- [ ] Só após proof-of-route adicionar frontend ao harness.

## Revisão obrigatória

- [ ] Cada full-frame copy é justificada.
- [ ] Sem sharing seguro = Blocked, não CPU fallback.
- [ ] Provenance de guides não é inferida.
- [ ] Legacy API não cria policy própria no core.

## Validação rápida

- [ ] Micro-harness prova Acquire+interop primeiro.
- [ ] Depois steady/resize com fake executor.
- [ ] Medir copies/sync/CPU.

## Gate

- [ ] Há rota GPU-resident demonstrável ou blocker técnico documentado.
- [ ] Nenhum código grande antes dessa decisão.

## Próxima fase

Fase 14.
