# Fase 15 — Universal guide acquisition

## Objetivo

Selecionar os melhores guides sem concentrar toda policy em um resolver gigante.

## Dependências

Carriers relevantes implementados.

## Fora de escopo

- Inventar provenance
- Exigir guides nativos

## Implementação

- [ ] Auditar/reusar PipelinePolicy, MotionVectorResolver, GuideValidation e NVOF.
- [ ] Preservar prioridade Native→DLSS contract→NVOF→shader→Zero.
- [ ] Centralizar depth reliability/exposure.
- [ ] Separar motion selection, validation e generation se responsabilidade crescer.
- [ ] Registrar source/reliability no FrameContract.
- [ ] Invalidar history em mudança material.
- [ ] Manter matriz API/provider→guides.
- [ ] Nenhum novo “GuideManager” multifunção >300 linhas.

## Revisão obrigatória

- [ ] Non-null != reliable.
- [ ] Zero é explícito.
- [ ] Não duplicar hierarquia já testada.
- [ ] Camera cut/reset prevalece.
- [ ] Módulos de guide <=300 linhas.

## Validação rápida

- [ ] Casos MotionVectorResolver existentes.
- [ ] Fake sources/provenance.
- [ ] Carriers com guides parciais.
- [ ] LOC checker.

## Gate

- [ ] Game sem DLSS tem estratégia explícita.
- [ ] Fallback observável.
- [ ] Carrier não contém policy duplicada.
- [ ] Arquivos tocados <=300 linhas.

## Próxima fase

Fase 16.
