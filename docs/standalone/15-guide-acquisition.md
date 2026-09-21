# Fase 15 — Universal guide acquisition

## Objetivo

Selecionar os melhores guides disponíveis sem tornar DLSS nativo requisito.

## Dependências

Carriers relevantes implementados.

## Fora de escopo

- Inventar provenance
- Exigir guides nativos

## Implementação

- [ ] Centralizar prioridade de motion: Native → DLSS contract → NVOF → shader → Zero.
- [ ] Centralizar depth reliability e exposure source.
- [ ] Definir custo/capability de NVOF e shader motion separadamente.
- [ ] Registrar source/reliability no FrameContract.
- [ ] Invalidar temporal history quando source muda materialmente.
- [ ] Policy/placement só usa capability realmente disponível.
- [ ] Manter matriz API/provider→color/depth/motion/exposure/fallback.

## Revisão obrigatória

- [ ] Non-null nunca implica reliable.
- [ ] Zero motion é declarado, nunca disfarçado.
- [ ] Fallback de guide não pode causar feature enablement indevido.
- [ ] Camera cut/reset prevalece sobre history.

## Validação rápida

- [ ] Fake sources alternando provenance sem GPU.
- [ ] Harness de carriers com guides ausentes/parciais.
- [ ] Status/diagnostics reportam source real.

## Gate

- [ ] Game sem DLSS ainda possui estratégia explícita.
- [ ] Fallback é observável e determinístico.
- [ ] Nenhum carrier contém policy duplicada de guide.

## Próxima fase

Fase 16.
