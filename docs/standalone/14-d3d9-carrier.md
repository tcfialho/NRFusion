# Fase 14 — D3D9/D3D9Ex carrier

## Objetivo

Provar D3D9Ex e D3D9 clássico separadamente sem crescer um legacy carrier monolítico.

## Dependências

Fases 07–08.

## Fora de escopo

- CPU screenshots
- Segundo NR runtime

## Implementação

- [ ] Mapear Present/Reset/lost-device/ownership.
- [ ] Proof-of-route D3D9Ex primeiro.
- [ ] Avaliar classic separadamente.
- [ ] Definir recursos realmente capturáveis.
- [ ] Bridge Host64/D3D12 quando GPU-resident for viável.
- [ ] Compose back/lost-device.
- [ ] Separar Ex/classic adapters se semântica divergir; cada arquivo <=300.

## Revisão obrigatória

- [ ] D3D9Ex/classic têm qualificação separada.
- [ ] Provider/bridge não prova Acquire.
- [ ] Sem transporte GPU = Blocked.
- [ ] Reset libera tudo.
- [ ] Format/colorspace conversion contabilizada.

## Validação rápida

- [ ] Micro-harness Ex.
- [ ] Reset/lost-device loops.
- [ ] Classic só com proof viável.
- [ ] LOC checker.

## Gate

- [ ] Cada variante Qualified ou Blocked com motivo.
- [ ] Nenhuma rota CPU vendida como normal.
- [ ] Código D3D9 tocado <=300 por arquivo.

## Próxima fase

Fase 15.
