# Fase 03 — Development Harness / MiniGame

## Objetivo

Dar à IA feedback determinístico, barato e independente de jogos reais.

## Dependências

Fases 01–02.

## Fora de escopo

- Game/engine real
- Assets/física/câmera
- Framework de testes pesado
- Validar qualidade visual de jogos reais

## Implementação

- [ ] Criar FakeNrExecutor e FakeTimingSource.
- [ ] Criar runner comum por CLI com seed/cenário/frames e saída JSON/texto.
- [ ] Suportar --executor=fake|dlss; fake deve funcionar sem GPU NVIDIA.
- [ ] Criar frontend D3D12 offscreen mínimo; swapchain só onde necessário.
- [ ] Gerar color/depth/motion/HDR sintéticos simples e determinísticos.
- [ ] Criar cenários steady, resize, reset, missing guides, provenance change, on/off, scale/precision change e failure injection.
- [ ] Medir wall CPU do NRFusion, p50/p95/p99, allocations observáveis, locks e resource/query creation counters.
- [ ] Separar warm-up da janela medida.
- [ ] Adicionar Harness32/Fake Streamline/frontends por API somente quando suas fases iniciarem.

## Revisão obrigatória

- [ ] Harness não pode duplicar policy do produto.
- [ ] Métrica deve envolver o código medido e excluir setup.
- [ ] Não usar CPU readback como atalho para uma rota que será GPU-resident.
- [ ] Mesmo comando/seed produz o mesmo evento lógico.
- [ ] Não construir tracer genérico caro se um contador local resolve.

## Validação rápida

- [ ] Fake executor por milhões de frames de policy/state.
- [ ] D3D12 fake-executor long run com resize/reset/failure.
- [ ] Relatório before/after consumível sem inspeção manual.
- [ ] Quando hardware existir, mesmo runner troca fake por dlss sem mudar cenário.

## Gate

- [ ] Existe loop implementar→executar→medir→revisar sem usuário.
- [ ] Harness permanece pequeno e reutilizável.
- [ ] Jogos reais deixam de ser ferramenta primária de diagnóstico.

## Próxima fase

Fase 04.
