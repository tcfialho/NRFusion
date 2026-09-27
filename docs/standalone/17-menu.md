# Fase 17 — Menu e configuração

## Objetivo

Manter menu simples e impedir que UI/config virem novo arquivo gigante.

## Dependências

Fases 02,06,16.

## Fora de escopo

- Replicar UI do OptiScaler
- Widgets ocultos por frame

## Implementação

- [x] Main model: Enabled, NR Mode, Target rendered FPS, Display Hz, MFG Mode/Quality e Status.
- [x] NR Mode: Auto / Best quality / Performance / Custom.
- [x] Target FPS continua sendo alvo renderizado; não é reinterpretado pelo multiplicador MFG.
- [x] Advanced contract separado com precision, appearance, exposure, placement/residual, multipass, MFG experimental e diagnostics.
- [x] Main, NR Advanced, MFG Advanced e Diagnostics têm contratos separados; nenhum owner novo excede 300 linhas.
- [x] Config parsing/storage separado do drawing.
- [x] Runtime e MFG publicam snapshots sem lock do render thread.
- [x] Helpers criados têm ownership específico: menu state, Main store, Advanced store e config validation.

HDR permanece fato do `FrameContract`/carrier. Não existe override de usuário inventado para contradizer o frame real; o controle Advanced correspondente é exposure.

## Revisão obrigatória

- [x] Hidden/inactive dos novos menu models não aloca VRAM nem dispara GPU work.
- [x] Abrir/fechar o menu model não muda lifetime/policy.
- [ ] Labels visuais finais rendered/generated FPS dependem do drawing/input no host; storage já usa `target_rendered_fps` explicitamente.
- [x] Config não cresce junto com código ImGui.
- [x] Arquivos standalone de UI/config respeitam <=300 linhas.

## Validação rápida

- [x] Harness/model menu closed/open.
- [x] Persist/reload/config inválida para Main e Advanced.
- [x] Parsing existe apenas em chamadas explícitas de store; nenhum parser foi ligado ao frame loop.
- [x] LOC checker.

## Evidência

- `e6e3d68`: contrato Main MFG/Display Hz.
- `580a13c`: snapshot lock-free do `RuntimeShell`.
- `e8a2847`: persistência Main versionada/fail-closed.
- `89ff3d1`: menu model com propose -> reconfigure -> accept.
- `f3ab028`: snapshot MFG POD/lock-free/sem string.
- `0194665`: `ProcessGetState` sem mutex de leitura.
- `2ea316d`: contrato Advanced reutilizando owners existentes.
- `b64284e`: persistência Advanced versionada/fail-closed.
- Portable `36321032932`: PASS.
- Focused Portable `36321032814`: PASS.
- Windows `36321032756`: PASS.
- Checkpoint: `nrfusion-source-b64284eac9a2a3bda759747f88a46121eb2606b7`,
  artifact `10931573645`,
  sha256 `a73063ca9d8ae0d5e3f597902f17b4702b4439f2a3074579bd22c3a916906bb7`.

## Gate

- [x] Main model continua pequeno.
- [x] Advanced default não ativa residual, multipass, diagnostics ou MFG experimental.
- [x] Owners standalone de UI/config respeitam <=300 linhas.
- [ ] Drawing/input real ainda vive no overlay legado do OptiScaler e no patcher >300 linhas.

**Subfase core/model da Fase 17 CLOSED. Fase 17 permanece IN PROGRESS até o cutover do drawing/input legado.**

Não criar uma segunda infraestrutura ImGui standalone apenas para fechar este checkbox. O cutover deve conectar os models/stores acima ao owner de overlay que substituir o patcher legado.

## Próxima fase

A Fase 18 pode avançar com este blocker explicitamente carregado; ela não depende do drawing do menu.
