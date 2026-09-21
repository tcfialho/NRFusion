# Fase 03 — Development Harness / MiniGame

## Objetivo

Criar feedback rápido para a IA sem jogos reais, com o mínimo de código.

## Dependências

Fases 01–02.

## Fora de escopo

- Engine/game real
- Assets/câmera/física
- UI elaborada
- Framework de teste genérico
- Validar FPS/qualidade de jogos reais

## Checklist de implementação

- [ ] Criar FakeNrExecutor sem GPU.
- [ ] Criar harness runner comum por CLI.
- [ ] Criar frontend D3D12 mínimo: device/queue/list/color/depth/motion/output.
- [ ] Gerar gradient, moving square, depth ramp, motion constante e HDR highlight.
- [ ] Cenários steady/resize/reset/missing-depth/missing-motion/changing-motion/HDR/on-off/scale/precision/failure.
- [ ] Contadores externos: ns/frame, p50/p95/p99, allocations, locks, resource creates, query commands.
- [ ] Separar warm-up da medição.
- [ ] Failure injection determinístico.
- [ ] Criar Harness32 na Fase 10.
- [ ] Criar Fake Streamline client na Fase 16.
- [ ] Adicionar frontends por API apenas quando a fase do carrier começar.

## Revisão obrigatória

- [ ] Harness não cria arquitetura paralela.
- [ ] Métricas fora do código medido quando possível.
- [ ] Nenhum CPU readback como atalho de futura rota GPU.
- [ ] Mesmo comando/seed reproduz cenário.
- [ ] Separar custo do harness do NRFusion.
- [ ] Instrumentação não pode distorcer hot path.

## Validação rápida

- [ ] FakeNrExecutor por centenas de milhares/milhões de frames.
- [ ] D3D12 steady longo com 0 allocations/resource creation após warm-up.
- [ ] Loops resize/reset/failure.
- [ ] Relatório before/after estruturado.

## Gate de conclusão

- [ ] Loop implementar->executar->medir->revisar sem usuário.
- [ ] Comando determinístico para benchmark.
- [ ] Harness permanece pequeno.
- [ ] Jogos reais deixam de ser ferramenta primária de diagnóstico.

## Entregáveis

- Fake executor
- MiniGame D3D12
- Scenario runner
- Performance report

## Próxima fase

Fase 04.
