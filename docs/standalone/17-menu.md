# Fase 17 — Menu e configuração

## Objetivo

Manter menu simples e impedir que UI/config virem novo arquivo gigante.

## Dependências

Fases 02,06,16.

## Fora de escopo

- Replicar UI do OptiScaler
- Widgets ocultos por frame

## Implementação

- [ ] Main: Enabled, NR Mode, Target FPS/Display Hz, MFG Mode/Quality e Status.
- [ ] NR Mode: Auto / Best quality / Performance / Custom.
- [ ] Target FPS continua sendo alvo renderizado; não reinterpretar pelo multiplicador MFG.
- [ ] Advanced collapsed com precision, appearance, HDR/exposure, placement/residual, multipass, MFG experimental e diagnostics.
- [ ] Separar Main, NR Advanced, MFG Advanced e Diagnostics UI quando necessário; nenhum menu source >300.
- [ ] Config parsing/storage separado do drawing.
- [ ] Status lê snapshot sem lock do render thread.
- [ ] UI helper só é extraído quando tem responsabilidade própria; nada de `MenuHelpers.cpp` depósito.

## Revisão obrigatória

- [ ] Hidden/inactive não aloca VRAM/GPU work.
- [ ] Abrir UI não muda lifetime/policy.
- [ ] Labels distinguem rendered/generated FPS.
- [ ] Config não cresce junto com código ImGui.
- [ ] Arquivos de UI/config <=300 linhas.

## Validação rápida

- [ ] Harness menu closed/open.
- [ ] Persist/reload/config inválida.
- [ ] 0 parsing/frame.
- [ ] LOC checker.

## Gate

- [ ] Main continua pequeno.
- [ ] Advanced não taxa default.
- [ ] UI/config inteira respeita <=300 por arquivo.

## Próxima fase

Fase 18.
