# Fase 20 — Auditoria de hot path e estrutura

## Objetivo

Provar performance e cumprir integralmente a arquitetura <=300 linhas antes de otimização final.

## Dependências

Rotas principais implementadas.

## Fora de escopo

- Tempo total do correctness harness como benchmark
- Refatoração estética sem responsabilidade clara

## Implementação

- [ ] Auditar allocation, containers, formatting, filesystem, scans, locks, resource/heap/PSO creation e waits.
- [ ] Auditar descriptor writes, queries, copies e capability/config queries.
- [ ] Classificar custo como init/reconfigure/steady.
- [ ] Gerar contagem/frame de locks/creates/writes/queries/copies/host calls.
- [ ] Rodar um checker pequeno sobre **todos** os arquivos first-party handwritten, não só touched files.
- [ ] Checker conta linhas físicas e usa allowlist apenas para generated/vendor/upstream fixtures.
- [ ] Classificar violações restantes: split agora ou retire antes do cutover.
- [ ] Split de test files grandes é por subsistema/cenário; build scripts por responsabilidade/target.
- [ ] Não criar framework complexo só para enforcement; checker deve permanecer pequeno.

## Revisão obrigatória

- [ ] Todo custo steady restante justificado.
- [ ] Benchmark exclui waits/Map/console de correctness.
- [ ] Warm-up separado; p50/p95/p99.
- [ ] Tail regression bloqueia merge.
- [ ] Nenhuma exclusão de LOC é criada só para evitar refactor.
- [ ] Minificação/embedded code não contam como solução.

## Validação rápida

- [ ] CPU/fake milhões de frames.
- [ ] Benchmark carriers principais.
- [ ] Correctness separado.
- [ ] Relatório completo de LOC first-party.

## Gate

- [ ] 0 heap allocation normal.
- [ ] 0 resource/heap/PSO creation normal.
- [ ] 0 filesystem/string/config parsing normal.
- [ ] 0 blocking wait normal.
- [ ] Arquivos novos/tocados: zero >300.
- [ ] Restante legado >300 tem fase de split/retirement explícita.

## Próxima fase

Fase 21.
