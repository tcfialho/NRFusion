# Fase 20 — Auditoria rígida de hot path

## Objetivo

Provar por revisão/harness que standalone é leve by design.

## Dependências

Rotas principais.

## Fora de escopo

- Microbenchmark desconectado
- Otimização especulativa do modelo

## Checklist de implementação

- [ ] Buscar new/delete/make_unique/shared.
- [ ] Vector/string growth.
- [ ] map/unordered_map insertion.
- [ ] std::function/format/streams.
- [ ] filesystem/module/PE scans.
- [ ] mutex/shared_mutex.
- [ ] CreateCommitted/PlacedResource/DescriptorHeap/QueryHeap/PSO.
- [ ] sync waits.
- [ ] capability/timestamp-frequency repetidos.
- [ ] string-to-enum/config conversion.
- [ ] Classificar init/reconfigure/steady.
- [ ] Registrar locks/resource creates/timestamps/resolves.

## Revisão obrigatória

- [ ] Todo steady occurrence precisa justificativa.
- [ ] p50/p95/p99.
- [ ] Separar warm-up.
- [ ] Instrumentação não distorce.
- [ ] Comparar host OptiScaler equivalente.

## Validação rápida

- [ ] Harness steady longo.
- [ ] Fake executor milhões de frames.
- [ ] Before/after por remoção.
- [ ] Reportar p99 regressão mesmo com média melhor.

## Gate de conclusão

- [ ] 0 heap alloc/frame.
- [ ] 0 GPU resource create/frame.
- [ ] 0 parsing/filesystem/frame.
- [ ] 0 blocking waits/frame.
- [ ] Locks inevitáveis documentados.
- [ ] CPU/p99 abaixo do baseline equivalente.

## Entregáveis

- Hot-path audit
- Perf baseline/standalone

## Próxima fase

Fase 21.
