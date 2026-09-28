# Fase 16 — MFG standalone

## Objetivo

Preservar MFG independente de NR decompondo patching e hot state.

## Dependências

Fases 02,04 e carrier aplicável.

## Fora de escopo

- MFG bloquear NR
- MFG universal sem rota comprovada

## Implementação

- [x] Rota A: jogos com Streamline/DLSSG. O core está preservado; harness e testes passam 100% (`nrfusion_mfg_capability_tests`, `nrfusion_mfg_fake_streamline_tests`). Publicação legada resolvida no cutover standalone da Fase 23.
- [x] Split `DlssgTransfusion.cpp`: **PE/fatbin/LZ4 parsing**, **module patch application**, **runtime option/state**, **safe transitions/publication**.
- [x] Preservar Game Controlled, multipliers, Quality, UI Recomposition e Safe Transition no core.
- [x] 5X/6X continuam experimentais quando suportados; flags isoladas do fluxo estável de 1X-4X sem regressão de latência.
- [x] Config vira enums/atomics fora do hot path: o core atende plenamente com tipos enumerados e POD snapshots no hot path.
- [x] Rota B sem DLSSG exige proof separado: **Blocked**, pois não existe executor MFG standalone nem proof sintético sem vetores de movimento.
- [x] Fake Streamline client <=300 por arquivo.
- [x] Capability MFG é independente de NR no core e permanece fail-closed quando não qualificada.

## Revisão obrigatória

- [x] SetOptions allocation-free/sem string work no caminho integrado: `DlssgTransfusion::ProcessSetOptions` passa com zero alocações em 250.000 iterações.
- [x] Patch/parser não roda per-frame; scans são qualificados no load e a tentativa vira terminal quando o módulo é encontrado.
- [x] Mutex removido do hot path e mantido apenas no estado compartilhado de patch/status.
- [x] GetState não mente capability no caminho integrado: core relata capability estritamente baseada em validação real de módulo e hardware.
- [x] Split segue lifecycle module/runtime.

## Validação rápida

- [x] Fake client: rapid changes, late load e failure.
- [x] Benchmark de `ProcessSetOptions`: loop de 250.000 chamadas e prova de zero allocations; sem threshold temporal frágil.
- [x] Proof separado MFG sintético: **Blocked** com justificativa técnica registrada (sem executor standalone nem proof sintético).
- [x] LOC checker: todos os módulos MFG standalone respeitam <= 300 linhas.

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

## Gate

- [x] Rota A preservada de ponta a ponta: validada via fake streamline client e harnesses de teste; blockers do host legado transferidos e eliminados pelo cutover da Fase 23.
- [x] Rota B provada ou Blocked: **Blocked**.
- [x] MFG <=300 por arquivo de integração: todos os módulos standalone de MFG (`DlssgTransfusionFatbin.cpp`, `DlssgTransfusionPatches.cpp`, `DlssgTransfusionRuntime.cpp`, `StreamlineMfgSession.cpp`) estão estritamente <= 300 linhas.

**Core/harness da Fase 16 concluído; integração de produto bloqueada.** O proxy standalone
distribuído não contém hook Streamline, portanto MFG não pode ser anunciado como rota integrada.

## Próxima fase

Fase 17.
