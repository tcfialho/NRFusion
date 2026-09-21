# Fase 14 — D3D9/D3D9Ex carrier

## Objetivo

Provar Acquire/transporte para D3D9Ex e D3D9 clássico separadamente antes de construir o carrier completo.

## Dependências

Fases 07–08.

## Fora de escopo

- CPU screenshots
- Segundo NR runtime

## Implementação

- [ ] Mapear Present/Reset/lost-device e ownership dos recursos.
- [ ] Fazer proof-of-route D3D9Ex primeiro.
- [ ] Avaliar D3D9 clássico separadamente sem assumir equivalência.
- [ ] Definir color/depth/motion realmente capturáveis.
- [ ] Bridge para Host64/D3D12 quando GPU-resident for viável.
- [ ] Compor de volta e tratar lost-device.
- [ ] Só então criar frontend correspondente.

## Revisão obrigatória

- [ ] D3D9Ex e classic têm estados de qualificação separados.
- [ ] Provider/bridge não prova Acquire.
- [ ] Sem transporte GPU viável = Blocked explícito.
- [ ] Reset/lost-device libera tudo.
- [ ] Conversion de format/colorspace é contabilizada.

## Validação rápida

- [ ] Micro-harness D3D9Ex Acquire+bridge.
- [ ] Reset/lost-device loops.
- [ ] Classic somente se proof-of-route justificar.

## Gate

- [ ] Cada variante termina Qualified ou Blocked com motivo.
- [ ] Nenhuma rota CPU é apresentada como suporte normal.

## Próxima fase

Fase 15.
