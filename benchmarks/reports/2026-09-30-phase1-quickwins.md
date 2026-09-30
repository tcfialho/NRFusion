# Fase 1 — Quick Wins de Performance: subgate QW1

**Fase 1 incompleta.** QW1 implementado e medido; QW2–QW6 não foram implementados nem rejeitados.
QW1 fechado na continuação: contagem dinâmica de Map/Unmap/CBV, falha de Map e recriação de device testadas.
A correção preexistente do Death Stranding foi preservada no commit `0ab4bc96d3608e32655bba6b4f5ed5c8598502ba`.

## Ambiente e protocolo

- HEAD inicial/final: `dac55cf8e38c537ed8f8af2681225918e612f646`; branch `standalone/integration`.
- RTX 4050 Laptop GPU SM89, driver 617.14; AC, plano Alto desempenho.
- FP8 NVIDIA real, Pre-SR, WorkingScale 1.0, uma passagem, MFG desligado.
- Executável oficial `dist/RequiemGame/RequiemGame.exe`, cena/movimento determinísticos.
- BEFORE e AFTER: 20 execuções cada, cinco repetições por resolução/on-off, 1200 frames, 300 de warmup.
- Percentis interpolados por execução; tabela usa mediana das cinco execuções.
- `nr_gpu_ms` inclui preparação/modelo/composição; tempo de parede contém Present/espera serial do testbed
  e não equivale a FPS de gameplay. CPU hook separado não foi medido neste subgate.
- Hashes de GPU/runtime/assets/binários e configurações completas: `comparison.json`.
- Baseline histórico lido: 720p 6.00269 ms, 1080p 9.66861 ms. A comparação desta tabela usa BEFORE fresco.
- W4A8 e FusedGroupedFfn não foram alterados, ativados ou usados como baseline.

## Resultado

| Change | Before | After | Delta | p95 | p99 | Correctness | Status |
|---|---:|---:|---:|---|---|---|---|
| Persistent CB (720p, NR GPU ms) | 5.975550 | 5.957630 | -0.300% | 6.190080 → 6.165500 | 6.478394 → 6.395290 | RGB exact nas capturas | Validado; não commitado |
| Persistent CB (1080p, NR GPU ms) | 9.540095 | 9.550850 | +0.113% | 9.967927 → 9.949440 | 10.997963 → 10.918064 | RGB exact nas capturas | Validado; não commitado |
| Logging | — | — | — | — | — | Não testado | QW2 pendente |
| Config cache | — | — | — | — | — | Não testado | QW3 pendente |
| Descriptor cache | — | — | — | — | — | Não testado | QW4 pendente |
| Zero-Division | — | — | — | — | — | Não testado | QW5 pendente |
| Direct guides | — | — | — | — | — | Não testado | QW6 pendente |

Não há ganho GPU conclusivo. A mudança elimina trabalho redundante: Map, Unmap e CBV creation saíram
de WriteConstants/dispatch e passaram para init/shutdown. Cada slot mantém seu upload resource e
mapped pointer; os mesmos 256 bytes são copiados por memcpy. Contagem dinâmica no codec test: init 48 Map/0 Unmap/48 CBV; dois dispatches mantêm
os contadores em 48/0/48; shutdown executa os 48 Unmap. A instrumentação está apenas no teste.

## Correctness e testes

- Build Release dos alvos oficiais proxy e codec test: PASS.
- `nrfusion_d3d12_nr_codec_tests` com `NRFUSION_TEST_D3D12_HARDWARE=1`: PASS.
- Teste confirma 48 buffers mapeados, GPU dispatch sem device removal, shutdown zerado,
  init nulo rejeitado e reinicialização bem-sucedida.
- Falha injetada no terceiro Map: init rejeitado; os dois maps anteriores são desfeitos;
  resources/mapped pointers zerados. Reinit e novo device: PASS.
- Capturas NR do frame final em 720p/1080p: RGB bit-identical BEFORE/AFTER.
- Isso não prova todos os frames/cenas de gameplay; nenhuma mudança algorítmica foi feita.
- p95/p99 do NR não pioraram nestas amostras. Ganho cumulativo da fase: não comprovado.
- Source-size contra HEAD e `git diff --check`: PASS.

## Estado e pendências

- Correção preexistente do Death Stranding commitada separadamente. QW1 será registrado em commit próprio.
- O testbed oficial usa o proxy QW1. Instalador e DLLs de jogos não foram atualizados nesta fase.
- Working tree ainda não está limpo; não declarar Definition of Done da fase atingida.
- CI do HEAD inicial: Windows e Portable PASS; Focused falha por dívidas preexistentes
  `src/RuntimeOverlayWindow.cpp` (322 linhas) e `tools/requiem_game/requiem_run_setup.inc` (351).
- Próxima ação: iniciar QW2 somente após registrar QW1; manter as medições isoladas.
