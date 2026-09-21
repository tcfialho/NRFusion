# Fase 03 — Development Harness / MiniGame

## Objetivo

Transformar a infraestrutura de teste existente no loop rápido principal da implementação, sem construir um game novo.

## Dependências

Fases 01–02.

## Fora de escopo

- Nova engine/harness paralelo
- Assets, física, câmera ou UI elaborada
- Framework de benchmark genérico
- Validar qualidade visual de jogos reais

## Implementação

- [ ] Auditar `nrfusion_sim`, `nrfusion_harness_3d`, synthetic tests e IPC tests; reutilizar antes de criar arquivos.
- [ ] Evoluir `nrfusion_harness_3d` para cenários CLI determinísticos em vez de criar outro D3D12 MiniGame.
- [ ] Separar executor do cenário: fake/simulado por padrão; DLSS real somente quando disponível.
- [ ] Reusar color/depth/motion/exposure que o harness 3D já produz.
- [ ] Adicionar apenas cenários faltantes: steady, resize, reset, missing guides, provenance change, on/off, scale/precision e failure injection.
- [ ] Padronizar saída curta JSON/texto com wall CPU, p50/p95/p99 e counters relevantes.
- [ ] Contar allocations/locks/resource/query creation com instrumentação local; não construir tracer genérico.
- [ ] Separar warm-up da janela medida.
- [ ] Frontends/API extras são extensões pequenas dos testes existentes durante suas próprias fases.

## Revisão obrigatória

- [ ] Harness não replica policy nem executor do produto.
- [ ] Métrica exclui setup e custo conhecido do próprio harness.
- [ ] Nenhum CPU readback vira atalho para uma rota de produto GPU-resident.
- [ ] Mesmo cenário/seed reproduz os mesmos eventos lógicos.
- [ ] A extensão proposta é menor que criar um novo executável equivalente.

## Validação rápida

- [ ] `nrfusion_sim` cobre loops longos puramente CPU.
- [ ] Harness 3D cobre lifecycle/resources sem exigir jogo.
- [ ] Synthetic/IPC tests continuam sendo probes focados onde já são melhores.
- [ ] Relatório before/after pode ser comparado sem interpretação manual extensa.

## Gate

- [ ] Existe loop implementar→executar→medir→revisar sem usuário.
- [ ] Nenhuma nova infraestrutura duplicada sem necessidade.
- [ ] Jogo real deixa de ser ferramenta primária de diagnóstico.

## Próxima fase

Fase 04.
