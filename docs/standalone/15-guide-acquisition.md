# Fase 15 — Universal guide acquisition

## Objetivo

Selecionar os melhores guides disponíveis sem tornar DLSS nativo requisito.

## Dependências

Carriers relevantes implementados.

## Fora de escopo

- Inventar provenance
- Exigir guides nativos

## Implementação

- [ ] Auditar/reusar `PipelinePolicy`, `MotionVectorResolver`, `GuideValidation` e NVOF existentes antes de criar nova policy.
- [ ] Manter prioridade explícita: Native → DLSS contract → NVOF → shader → Zero.
- [ ] Centralizar depth reliability e exposure source.
- [ ] Definir custo/capability de NVOF e shader motion separadamente.
- [ ] Registrar source/reliability no FrameContract.
- [ ] Invalidar temporal history quando source muda materialmente.
- [ ] Manter matriz API/provider→color/depth/motion/exposure/fallback.

## Revisão obrigatória

- [ ] Non-null nunca implica reliable.
- [ ] Zero motion é declarado, nunca disfarçado.
- [ ] Não duplicar hierarquia já validada no Synthetic DX11 test.
- [ ] Camera cut/reset prevalece sobre history.

## Validação rápida

- [ ] Reusar casos do MotionVectorResolver existentes.
- [ ] Fake sources alternando provenance.
- [ ] Carriers com guides ausentes/parciais.
- [ ] Status reporta source real.

## Gate

- [ ] Game sem DLSS ainda possui estratégia explícita.
- [ ] Fallback é observável e determinístico.
- [ ] Carrier não contém policy duplicada de guide.

## Próxima fase

Fase 16.
