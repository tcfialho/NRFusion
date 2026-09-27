# Fase 16 — MFG standalone

## Objetivo

Preservar MFG independente de NR decompondo patching e hot state.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- MFG bloquear NR
- MFG universal sem rota comprovada

## Implementação

- [ ] Rota A: jogos com Streamline/DLSSG. O core está preservado; a publicação final no host legado continua bloqueada.
- [x] Split `DlssgTransfusion.cpp`: **PE/fatbin/LZ4 parsing**, **module patch application**, **runtime option/state**, **safe transitions/publication**.
- [x] Preservar Game Controlled, multipliers, Quality, UI Recomposition e Safe Transition no core.
- [ ] 5X/6X continuam experimentais quando suportados; falta qualificação dedicada.
- [ ] Config vira enums/atomics fora do hot path. O core atende; o hook Streamline legado ainda faz parsing de strings em SetOptions.
- [x] Rota B sem DLSSG exige proof separado: **Blocked**, pois não existe executor MFG standalone nem proof sintético.
- [x] Fake Streamline client <=300 por arquivo.
- [x] Capability MFG é independente de NR no core e permanece fail-closed quando não qualificada.

## Revisão obrigatória

- [ ] SetOptions allocation-free/sem string work no caminho integrado. `DlssgTransfusion::ProcessSetOptions` passa; o patcher legado ainda faz string work antes da chamada.
- [x] Patch/parser não roda per-frame; scans são qualificados no load e a tentativa vira terminal quando o módulo é encontrado.
- [x] Mutex removido do hot path e mantido apenas no estado compartilhado de patch/status.
- [ ] GetState não mente capability no caminho integrado. O core passa; o patcher legado ainda força Dynamic MFG como suportado.
- [x] Split segue lifecycle module/runtime.

## Validação rápida

- [x] Fake client: rapid changes, late load e failure.
- [x] Benchmark de `ProcessSetOptions`: loop de 250.000 chamadas e prova de zero allocations; sem threshold temporal frágil.
- [ ] Proof separado MFG sintético: **Blocked**, sem executor standalone.
- [x] LOC checker.

## Evidência desta fase

- `01e0d4c`: split inicial dos owners MFG.
- `9000431`: capability fail-closed; kernels + gates exatos antes de publicar MFG.
- `8a754b9`: `ProcessSetOptions` sem mutex no hot path.
- `a898106`: signatures são validadas antes de qualquer mutação de fatbin.
- `2816905`: late load/failure observáveis e fail-closed.
- `82e7561`: zero allocations no hot path e benchmark determinístico.
- `e9f5d3d`: fake Streamline client dedicado.
- Windows de `e9f5d3d`: `36287008142` PASS.
- Focused Portable de `e9f5d3d`: `36287008179` PASS.
- Portable de `e9f5d3d`: `36287008137` PASS.
- Checkpoint: `nrfusion-source-e9f5d3d4301effe97b680b5162fe45b3c3d65ca5`,
  artifact `10921040713`,
  sha256 `beaf3c5c392d8ee76565855a0344ef2de3d453cf822f573f81d2281d7c4bb154`.

## Blockers restantes

1. `tools/apply_to_optiscaler.py` ainda faz parsing de `ControlMode`, `QualityMode` e `UiRecomposition` dentro do hook de SetOptions.
2. O mesmo patcher ainda escreve `bIsDynamicMFGSupported = true` e `dlssgGameDMFGSupported = true` sem condicionar à capability comprovada.
3. O patcher possui 3900 linhas. Qualquer modificação direta viola `check_source_size.py changed`; o baseline determina sua remoção, não refatoração, na Fase 23.
4. A Rota B sem DLSSG não possui executor/hook/proof standalone e permanece **Blocked**.

## Gate

- [ ] Rota A preservada de ponta a ponta: bloqueada pelo host legado acima.
- [x] Rota B provada ou Blocked: **Blocked**.
- [ ] MFG <=300 por arquivo de integração: owners standalone passam; o patcher legado ainda contém integração MFG e será removido na Fase 23.

**Fase 16 IN PROGRESS.** Não anunciar Dynamic MFG nem Rota B como qualificadas antes da remoção dos blockers.

## Próxima fase

Fase 17 somente após o gate da Fase 16 ou com os blockers explicitamente carregados para o cutover.
