# Fase 08 — Timing e Diagnostics desacoplados

## Objetivo

Manter timing suficiente ao controller com módulos pequenos e custo detalhado só sob demanda.

## Dependências

Fase 07.

## Fora de escopo

- Remover diagnóstico
- Copiar waits bloqueantes do harness para o produto

## Implementação

- [x] Query ring, readback e associação WorkId ficam em componentes focados.
- [x] Normal path: um intervalo total; alvo 2 timestamps + 1 resolve por amostra.
- [x] Consumir resultado aposentado sem esperar GPU atual.
- [x] Cachear timestamp frequency por queue.
- [x] Model/resolve timing só em Diagnostics.
- [x] FakeTimingSource usa a mesma interface.
- [x] Se timing + diagnostics se aproximarem de 250 linhas, separar coleta de apresentação/status.

## Revisão obrigatória

- [x] Slot não reutiliza antes do retirement.
- [x] Stale timing não reaplica.
- [x] Diagnostics off não mantém recursos/comandos exclusivos.
- [x] Map/readback não introduz sync implícita.
- [x] Nenhum arquivo <=300 é mantido artificialmente por código comprimido.

## Validação rápida

- [x] Fake timing valida association/staleness.
- [x] Correctness harness valida queries reais.
- [x] Benchmark exclui waits artificiais.
- [x] Checker de LOC.

## Gate

- [x] Produto normal não espera GPU por telemetry.
- [x] Off não paga On.
- [x] Timing/diagnostics <=300 linhas por arquivo.

## Próxima fase

Fase 09.


## Subgate 08a ? audit e boundary de Diagnostics

A auditoria inicial confirmou que NrSession j? ? o owner da identidade de timing:
- MapTimedWork s? aceita WorkTicket submetido e ainda live;
- NrSessionTimingQueue mant?m a associa??o exata at? o retirement;
- RetireTimedInterval completa o ticket antes de aplicar custo de precis?o/escala;
- o TimingWorkMapper legado continua restrito ao adapter legado e n?o ser? duplicado no standalone.

Diagnostics.cpp foi reduzido de 285 para 248 linhas sem novo source no patcher legado.
Persist?ncia/trace ficam junto da API em Diagnostics.hpp; o .cpp fica dedicado ? apresenta??o.

A coleta D3D12 existente em NrD3D12Diagnostics.cpp permanece um caminho de profiling expl?cito:
ela usa dois timestamps + um resolve por intervalo e s? l? ap?s fence j? conclu?do, mas ainda consulta
GetTimestampFrequency a cada leitura e mant?m query/readback pr?prios do diagn?stico detalhado.

Pr?ximo subgate exato:
1. definir um contrato port?til de timing aposentado e FakeTimingSource;
2. associar cada amostra a um WorkTicket exato sem criar outro ledger;
3. criar o owner D3D12 de query ring/readback com frequ?ncia cacheada no bind;
4. nunca esperar a GPU atual: retirement s? consome slot cujo completion j? foi observado;
5. manter model/resolve profiling exclusivo do caminho Diagnostics.

### Subgate 08b/08c ? retired timing contract and D3D12 owner

- `NrRetiredTimingSample` now carries exact `WorkTicket` identity; `NrSession` rejects out-of-order samples without consuming the queued mapping.
- `FakeTimingSource` validates bounded capacity, no slot reuse before retirement, monotonic completion values, invalid samples, and zero heap allocation over 100k iterations.
- `NrSession` caches one retired NR timing and exposes it to the controller exactly once on the next resolved frame; stale values are not replayed.
- `D3D12RetiredTimingSource` owns an 8-slot timestamp query/readback ring, records exactly two timestamps plus one resolve per sampled workload, and caches queue timestamp frequency during bind.
- `TryRetire()` only maps readback after the caller reports the completion value as retired; the source contains no fence wait or current-work synchronization.
- The hardware test is opt-in with `NRFUSION_TEST_D3D12_HARDWARE=1`; default CTest skips before creating WARP, avoiding the known synthetic-driver hang path.

Exact next action:
- wire `D3D12RetiredTimingSource` into the real D3D12 submission owner that has queue/fence completion values;
- map successful work with its exact `WorkTicket`, convert failed/unsampled attempts to invalid timing entries, and drain retired samples through `D3D12CarrierSession::RetireTimedSample`;
- keep model/resolve profiling exclusive to Diagnostics and preserve the no-wait normal path.

## Fechamento ? 2026-09-23

Fase 08: **CLOSED** no boundary standalone.

Evid?ncias finais:
- contrato port?til e FakeTimingSource cobrem associa??o exata, staleness, capacity/slot reuse e 100k itera??es sem heap;
- D3D12RetiredTimingSource usa ring fixo, 2 timestamps + 1 resolve, frequ?ncia cacheada e s? mapeia readback depois do completion observado;
- gate integrado em hardware real valida D3D12 query/fence -> NrRetiredTimingSample -> D3D12CarrierSession -> controller e confirma consumo ?nico;
- src/D3D12RetiredTimingSource.cpp n?o cont?m wait/event/sleep de GPU; qualquer espera do teste fica fora do produto;
- MinGW/Windows com hardware: 37/37 CTest PASS; MSVC focado: 3/3 PASS;
- LOC: Diagnostics.cpp 248, NrD3D12Diagnostics.cpp 254, D3D12RetiredTimingSource.cpp 175; nenhum componente de timing/diagnostics excede 300 linhas;
- checker de tamanho continua apontando apenas o shader vendor locked j? conhecido.

Gate integrado: 3726dca.

O cutover f?sico provider/Host64/patcher n?o pertence a esta fase; permanece diferido para o cutover global da Fase 23.

Pr?xima fase: Fase 09 ? D3D11 x64 bridge/carrier.
