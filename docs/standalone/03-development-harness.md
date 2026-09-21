# Fase 03 — Development Harness / MiniGame

## Objetivo

Transformar a infraestrutura de teste existente no loop rápido principal, sem construir um game novo.

## Dependências

Fases 01–02.

## Fora de escopo

- Nova engine/harness paralelo
- Assets, física, câmera ou UI elaborada
- Framework de benchmark genérico
- Validar qualidade visual de jogos reais

## Implementação

- [ ] Auditar `nrfusion_sim`, `nrfusion_harness_3d`, synthetic tests e IPC tests; reutilizar antes de criar arquivos.
- [ ] Evoluir `nrfusion_harness_3d` por CLI em vez de criar outro D3D12 MiniGame.
- [ ] Separar `correctness` de `benchmark`: o modo atual com fence waits/Map é válido para correção, não para medir host overhead.
- [ ] Em benchmark, medir CPU/fake path sem waits artificiais, console por frame ou export durante a janela medida.
- [ ] Reusar color/depth/motion/exposure que o harness 3D já produz.
- [ ] Adicionar só cenários faltantes: steady, resize, reset, missing guides, provenance change, on/off, scale/precision e failure.
- [ ] Padronizar saída curta com p50/p95/p99 e counters; `metrics_.reserve(frameCount)`/buffers pre-sized antes da janela.
- [ ] Contar allocations/locks/resource/query creation com instrumentação local, não tracer genérico.
- [ ] Frontends/API extras são extensões pequenas dos testes existentes nas fases correspondentes.

## Revisão obrigatória

- [ ] Harness não replica policy/executor do produto.
- [ ] Métrica exclui setup, waits de correção e custo conhecido do harness.
- [ ] Nenhum CPU readback vira atalho para rota de produto GPU-resident.
- [ ] Mesmo cenário/seed reproduz os mesmos eventos lógicos.
- [ ] Extender o existente deve ser menor que criar executable equivalente.

## Validação rápida

- [ ] `nrfusion_sim` cobre loops longos puramente CPU.
- [ ] Harness 3D em correctness cobre lifecycle/resources.
- [ ] Modo benchmark mede apenas o trecho declarado.
- [ ] Synthetic/IPC tests permanecem probes focados.
- [ ] Relatório before/after é comparável automaticamente.

## Gate

- [ ] Existe loop implementar→executar→medir→revisar sem usuário.
- [ ] Correção e performance não contaminam uma à outra.
- [ ] Nenhuma infraestrutura duplicada sem necessidade.
- [ ] Jogo real deixa de ser ferramenta primária de diagnóstico.

## Próxima fase

Fase 04.
