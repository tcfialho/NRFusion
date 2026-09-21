# Fase 14 — D3D9/D3D9Ex carrier

## Objetivo

Bridge legado distinguindo D3D9Ex do D3D9 clássico.

## Dependências

Fases 07–08.

## Fora de escopo

- CPU screenshots
- Segundo NR runtime

## Checklist de implementação

- [ ] Interception route.
- [ ] Capabilities D3D9Ex/classic.
- [ ] GPU transfer/share.
- [ ] Color.
- [ ] Depth strategy.
- [ ] Motion strategy.
- [ ] Bridge Host64/D3D12.
- [ ] Compose back.
- [ ] Reset/lost-device.
- [ ] Frontend mínimo.

## Revisão obrigatória

- [ ] Não fingir GPU-resident.
- [ ] Lost-device cleanup.
- [ ] Format/colorspace.
- [ ] Interop ownership/sync.

## Validação rápida

- [ ] Harness D3D9Ex e classic se viável.
- [ ] Reset/lost-device loops.
- [ ] Measure transfer.

## Gate de conclusão

- [ ] Cada variante Qualified ou Blocked com razão.
- [ ] Nenhuma rota CPU disfarçada.

## Entregáveis

- D3D9 carrier
- Capability split
- Harness

## Próxima fase

Fase 15.
