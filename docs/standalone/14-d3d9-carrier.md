# Fase 14 — D3D9/D3D9Ex carrier

## Objetivo

Separar D3D9Ex e D3D9 clássico e provar transporte viável antes de construir o carrier.

## Dependências

Fases 07–08.

## Fora de escopo

- CPU screenshots
- Segundo NR runtime

## Implementação

- [ ] Mapear interception/reset/lost-device semantics.
- [ ] Fazer proof-of-route D3D9Ex→shared/bridge primeiro.
- [ ] Avaliar D3D9 clássico separadamente sem assumir equivalência.
- [ ] Definir color/depth/motion possíveis e provenance.
- [ ] Bridge para Host64/D3D12 quando GPU-resident for viável.
- [ ] Compor de volta e tratar lost-device.
- [ ] Só então criar frontend correspondente.

## Revisão obrigatória

- [ ] D3D9Ex e classic têm estados de qualificação separados.
- [ ] Sem transporte GPU viável = Blocked explícito.
- [ ] Reset/lost-device libera tudo.
- [ ] Colorspace/format conversions são contabilizados.

## Validação rápida

- [ ] Micro-harness D3D9Ex proof-of-route.
- [ ] Reset/lost-device loops.
- [ ] Classic apenas se a rota técnica justificar implementação.

## Gate

- [ ] Cada variante termina como Qualified ou Blocked com motivo concreto.
- [ ] Nenhuma rota CPU é vendida como suporte normal.

## Próxima fase

Fase 15.
