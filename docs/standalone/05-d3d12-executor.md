# Fase 05 — Executor DLSS 5 D3D12 canônico

## Objetivo

Extrair o executor maduro preservando semântica, mas **não** recriar o monólito de ~175 KB em outro arquivo.

## Dependências

Fase 04.

## Fora de escopo

- Otimizar barriers/copies
- Trocar algoritmo visual
- Segunda interface NGX

## Implementação

- [ ] Antes de mover código, desenhar boundaries por responsabilidade: feature lifecycle, resources, dispatch, state/barriers, residual, exposure/HDR e diagnostics.
- [ ] Mover mecanicamente por boundary; comportamento fica idêntico durante o split.
- [ ] Cada source/header handwritten fica <=300 linhas; alvo <=250.
- [ ] Reusar `HostDlssNr`/forwarder como referência da ABI/call sequence.
- [ ] Manter uma única boundary de chamada ao modelo.
- [ ] Substituir OptiScaler Config/State por parâmetros/snapshot.
- [ ] Preservar pending-submission/rebuild, subrect/padding, pre/post-SR/RR/history, scale, multipass, HDR/exposure/residual e failure latches.
- [ ] Shaders handwritten também obedecem 300 linhas; código gerado é artefato separado e marcado como gerado.

## Revisão obrigatória

- [ ] Split segue ownership/lifetime, nunca `ExecutorPart1/Part2`.
- [ ] Cada resource tem owner/create/state/release/resize/failure.
- [ ] Cada barrier tem estado anterior/próximo/caller guarantee.
- [ ] Driver module/forwarder/model DLL têm load policy explícita.
- [ ] Nenhuma otimização funcional escondida no split.
- [ ] Split não adiciona interface virtual/heap/lock só para criar boundaries; componentes internos podem permanecer static/compile-time.

## Validação rápida

- [ ] Revisar diff mecânico por responsabilidade.
- [ ] Se mover hot helpers entre translation units, comparar host CPU before/after para detectar perda de inlining.
- [ ] Loop reset/resize/rebuild com substitutes quando possível.
- [ ] Checker <=300 em toda árvore extraída.
- [ ] Modelo real só no gate de hardware.

## Gate

- [ ] Sem OptiScaler direto.
- [ ] Uma call boundary DLSS-NR.
- [ ] Resource/state map completo.
- [ ] Zero arquivo handwritten >300 no executor extraído.

## Próxima fase

Fase 06.
