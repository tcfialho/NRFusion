# Fase 13 — D3D10 carrier

## Objetivo

Provar rota mínima D3D10 sem criar código grande antes de saber se o interop é viável.

## Dependências

Fases 07–08.

## Fora de escopo

- Executor NR D3D10
- CPU screenshot

## Implementação

- [ ] Localizar Acquire seam/ownership D3D10/DXGI.
- [ ] Criar proof-of-route GPU-resident em arquivo(s) pequenos.
- [ ] Definir color/depth/motion realmente acessíveis.
- [ ] Bridge D3D12 + compose back.
- [ ] Sync sem blocking recorrente.
- [ ] Só após prova, promover prototype a carrier modular.
- [ ] Nenhum prototype vira arquivo >300 “temporariamente”.

## Revisão obrigatória

- [ ] Cada full-frame copy justificada.
- [ ] Sem sharing seguro = Blocked.
- [ ] Guides não inferidos.
- [ ] Prototype descartável não duplica core policy.

## Validação rápida

- [ ] Micro-harness Acquire+interop.
- [ ] Depois steady/resize.
- [ ] Medir copies/sync/CPU.
- [ ] LOC checker.

## Gate

- [ ] Rota demonstrável ou blocker técnico.
- [ ] Nenhum arquivo >300 no proof/carrier.

## Próxima fase

Fase 14.
