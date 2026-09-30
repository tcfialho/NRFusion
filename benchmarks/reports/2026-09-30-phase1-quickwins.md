# Fase 1 — Quick Wins de Performance: subgate QW1

**Fase 1 incompleta.** QW1 e QW2 mantidos; QW3 revertido e QW4 descartado; QW5–QW6 pendentes.
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
| Config cache (µs/getter) | 0.014063 | 0.001563 | -88.889% | 0.014063 → 0.001563 | 0.017969 → 0.001563 | Snapshots coerentes; RGB exact | Revertido por falta de ganho no pipeline | |
| Descriptor cache | Referência 4ed0d9b | Protótipo testado | NR+DLSS p50 pareado -0.018% | Sem ganho consistente | Outliers no teste | RGB exact | Rejeitado; código restaurado |
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

## QW3 — confirmação de tails em 1080p

- A pedido do usuário, 720p está excluído das próximas medições. Input DLSS do teste 1080p permanece
  1280x720, como no baseline Pre-SR; não houve redução de WorkingScale/resolução neural para obter ganho.
- Referência anterior ao QW3: commit `41b0828`; candidato: `09abdc4`.
- Referência compilada do source exato desse commit em diretório isolado, com o mesmo MSVC/dependências.
- Ambos executaram no mesmo `dist/RequiemGame/RequiemGame.exe`; apenas a DLL foi alternada.
- Seis pares A/B, alternando a ordem a cada par; 1200 frames, 300 warmups e 900 amostras por execução.
- FP8, WorkingScale 1.0, Pre-SR, uma passagem, MFG off. Shader headers têm hashes idênticos.
- Capturas RGB idênticas. DLL candidata e configurações originais restauradas ao terminar.
- p95 NR, mediana das execuções: 11.3454925 → 11.2389225 ms.
  Mediana das diferenças pareadas: -0.3967%; pares variaram de -4.46% a +3.40%.
- p50 NR: 9.892605 → 9.90003 ms; p99 NR: 11.877497 → 11.9066375 ms.
- NR+DLSS p95: 13.1794 → 13.198155 ms, diferença de aproximadamente +0.14% entre medianas.
- A piora inicial de +2.22% no p95 NR não se repetiu como regressão consistente neste A/B.
  Não é prova de equivalência em todos os jogos nem ganho GPU; QW3 mantido pela remoção do mutex/cópia.
- Resultado completo e percentis de cada par: `../phase1/2026-09-30-qw3-tail-1080p/paired-result.json`.

## Decisão posterior do usuário — QW3 revertido

- O usuário passou a exigir ganho real comprovado, rejeitando o getter isolado como justificativa de manutenção.
- QW3 revertido no código, sem reescrever histórico; medições de CPU/GPU e A/B continuam preservadas.
- QW1 (`eb3e984`) e QW2 (`e1731b8`) permanecem ativos. A restauração usa o código de `41b0828`,
  que já contém ambos; arquivos de codec e Logger não foram revertidos.
- Rejeição: ganho GPU/FPS não comprovado. A/B sem regressão relevante não é prova de ganho.
- Próxima etapa: QW4 descriptor caching, medido em 1080p; manter apenas se o ganho for comprovado.

## QW4 — descartado por falta de ganho convincente

- Cache fixo de SRV/UAV por slot e identidade COM, com retenção da resource para evitar reuso falso de ponteiro.
- Prototype test GPU: 48 slots preenchidos, 240 SRV/96 UAV creates; mais 48 dispatches não aumentam os contadores.
  Resource trocada reconstrói SRVs. Nenhuma alocação/PSO/heap por frame foi adicionada.
- A/B 1080p, seis pares em ordem alternada, 1200 frames e 300 warmups, FP8/scale1/Pre-SR/1pass/MFG off.
- NR+DLSS p50 pareado: -0.0182%; frame-wall p50 pareado: praticamente 0%.
- NR GPU p50 pareado: -0.1513%, insuficiente para afirmar ganho relevante no pipeline.
- Outliers em alguns pares impediram atribuir grandes diferenças de cauda ao cache; causa não comprovada.
- Capturas RGB exact. Cache eliminado do produto por decisão de manutenção mais estrita do usuário.
- Patch do protótipo, contadores e dados completos preservados em `../phase1/2026-09-30-qw4-rejected`.
- QW1 e QW2 continuam; QW3 e QW4 descartados. Próxima etapa: QW5 Zero-Division PTX.
