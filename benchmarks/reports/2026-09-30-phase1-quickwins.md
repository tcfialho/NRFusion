# Fase 1 — Quick Wins de Performance: subgate QW1

**Fase 1 incompleta.** QW1–QW3 implementados e medidos; QW4–QW6 pendentes.
QW1 fechado na continuação: contagem dinâmica de Map/Unmap/CBV, falha de Map e recriação de device testadas.
A correção preexistente do Death Stranding foi preservada no commit `0ab4bc96d3608e32655bba6b4f5ed5c8598502ba`.

## Ambiente e protocolo

- Commit inicial da fase: `dac55cf8e38c537ed8f8af2681225918e612f646`; último commit de código validado:
  `e1731b8a68f17fbf11e599ea89c21abe6ab74457`; branch `standalone/integration`.
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
| Persistent CB (720p, NR GPU ms) | 5.975550 | 5.957630 | -0.300% | 6.190080 → 6.165500 | 6.478394 → 6.395290 | RGB exact nas capturas | Mantido; eb3e984 |
| Persistent CB (1080p, NR GPU ms) | 9.540095 | 9.550850 | +0.113% | 9.967927 → 9.949440 | 10.997963 → 10.918064 | RGB exact nas capturas | Mantido; eb3e984 |
| Logging (µs/INFO call) | 2009.850 | 3.400 | -99.831% | 2100.620 → 3.800 | 2205.485 → 6.302 | Mensagens preservadas; RGB exact | Mantido; e1731b8 |
| Config cache (µs/getter) | 0.014063 | 0.001563 | -88.889% | 0.014063 → 0.001563 | 0.017969 → 0.001563 | Snapshots coerentes; RGB exact | Mantido; commit próprio |
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

- Correção preexistente: `0ab4bc9`. QW1: `eb3e984`. QW2: `e1731b8`. Commits independentes; todos locais.
- O testbed oficial usa o proxy QW1. Instalador e DLLs de jogos não foram atualizados nesta fase.
- Working tree limpo após os commits desta sessão; Definition of Done da fase NÃO atingida, pois QW3–QW6 estão pendentes.
- CI do HEAD inicial: Windows e Portable PASS; Focused falha por dívidas preexistentes
  `src/RuntimeOverlayWindow.cpp` (322 linhas) e `tools/requiem_game/requiem_run_setup.inc` (351).
- Próxima ação exata: QW3, auditar todos os escritores da configuração, medir BEFORE, implementar cache por generation, testar concorrência e medir AFTER.

## QW2 — encerramento

- QW1 registrado em `eb3e9844a7955f77c13791c241b916bd36f05539`.
- Logger INFO/DEBUG/WARN usa WriteFile sem FlushFileBuffers; ERROR preserva flush imediato.
- Flush explícito no shutdown do proxy e no fechamento do logger.
- Log a cada 120 quadros do NeuralHook removido; logs de initialization/erro/recreation/status mantidos.
- CPU do logger: **EXPERIMENTO ISOLADO — NÃO VALIDA O PRODUTO FINAL** como ganho de FPS.
  Compila Logger.cpp real com wrappers de contagem de I/O apenas no teste; cinco runs de 1000 INFO calls,
  100 warmups/run. Os contadores registram 1000 writes em ambos e flushes 1000 → 0.
- Teste de política do logger e teste do proxy: PASS. INFO/DEBUG/WARN/ERROR continuam no arquivo,
  ERROR faz flush e Flush explícito funciona.
- Pipeline oficial Requiem BEFORE/AFTER: 20 runs cada, 360 frames/run, 60 warmups/run, cinco repetições,
  NR scale 1.0, FP8, Pre-SR, uma passagem e MFG off. Capturas 720p/1080p RGB bit-identical.
- GPU p50/p95/p99 e frame-wall p50/p95/p99 estão em `../phase1/2026-09-30-qw2/comparison.json`.
  Frame-wall é serial/contém espera do testbed, não FPS de gameplay. Ganho cumulativo de FPS não foi comprovado.
- Alteração mantida pela eliminação de flushes normais e logging periódico; nenhum algoritmo visual mudou.
- QW3–QW6 não executados nesta sessão por limite de 20 minutos do AGENT.md.

## Limitações atuais

- Push e CI dos commits novos ainda não executados: aguardam autorização de publicação.
- Sem qualificação de gameplay ampla para as otimizações; o gate físico executado foi o pipeline oficial Requiem.
- Instalador e DLLs nos jogos continuam na versão anterior à Fase 1; o proxy otimizado foi aplicado no Requiem.

## QW3 — validação

- Snapshot com atomic generation check antes do lock; campos copiados somente quando a versão muda.
- Token monotônico próprio, distinto da generation da política: reload/reinitialize também invalidam o cache.
- Load de configurações agora protegido pelo mesmo mutex; publicação depois de load/apply/cópia do shell.
- Hook NGX e proxy direto do Requiem guardam snapshot por feature, sem heap/mutex novo por evaluate.
- GameNeuralControl::Reconfigure deixa de ser chamado a cada frame quando a configuração não mudou.
- Benchmark CPU: **EXPERIMENTO ISOLADO — NÃO VALIDA O PRODUTO FINAL** como ganho de FPS.
  Getter real, sem writer concorrente: 2000 blocos de 128 chamadas, cinco repetições, clock fora de cada bloco.
  AFTER confirma um único snapshot em 257000 acessos. Não são tempos do hook completo.
- Teste mantém mutex do overlay bloqueado no mesmo thread e confirma retorno imediato com versão igual.
- Testes: apply real; 10000 publicações concorrentes sem pares main/advanced incoerentes; reload/reinit PASS.
- O primeiro teste de reinit falhou porque o fixture anterior deixava o overlay inicializado;
  fixture corrigido com Shutdown inicial. Testes focados repetidos: 2/2 PASS.
- Pipeline oficial Requiem: protocolo pareado de 360 frames/60 warmup/cinco repetições, FP8, scale 1.0,
  Pre-SR, uma passagem, MFG off. Capturas 720p/1080p RGB bit-identical.
- GPU/frame-wall p50/p95/p99: `../phase1/2026-09-30-qw3/comparison.json`.
  Nenhum ganho cumulativo de FPS é atribuído a esta medição. QW4–QW6 ainda pendentes.

### Ressalva de timing QW3

NR GPU 720p: p99 6.644355 → 6.854803 ms (+3.17%); BEFORE variou aproximadamente 10.51% entre runs e AFTER 5.94%. P95 1080p +2.22%. O teste curto não estabelece regressão estatisticamente significativa nem ganho GPU; manter pelo mutex/cópia eliminados e correctness, com confirmação de tails no A/B cumulativo final. CPU getter muito curto e quantizado pelo clock: não extrapolar nanosegundos para FPS.
