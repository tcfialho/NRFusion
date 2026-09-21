# Fase 05 — Executor DLSS 5 D3D12 canônico

## Status

**Em andamento.** Primeiro subgate concluído: canonicalização/split do seed standalone.
Evidência parcial: [05-d3d12-executor-evidence.md](05-d3d12-executor-evidence.md).

## Objetivo

Extrair o executor maduro preservando semântica, sem recriar o monólito OptiScaler.

## Dependências

Fase 04 concluída e revisada.

## Fora de escopo

- Otimizar barriers/copies
- Trocar algoritmo visual
- Segunda interface NGX
- Instalar carrier/hook D3D12 da Fase 07

## Auditoria inicial — 2026-09-21

### Seed standalone existente

O antigo `HostDlssNr` já possuía load policy, capability params, feature primária,
`JustBuilt()` e uma boundary `dlssnr_call_evaluate_v2`.

### Executor maduro de referência

`tests/fixture/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp` possui ~3883 linhas e permanece
read-only. Boundaries mapeados:

- loader/forwarder/capability params: ~521–732;
- retirement/rebuild/resources: ~733–820 e ~1815–2200;
- state/barriers/format: ~1177–1413;
- primary/multipass create + pending submission: ~1815–2185;
- scale/subrect/guides/encode/dispatch: ~2200–2720;
- resolve/HDR/residual: ~2720–3120;
- public pre/post SR/RR seams: ~3128–3539;
- shutdown/lifetime: ~3705–3755.

## Boundaries alvo

1. `D3D12NrExecutor` — API pública e snapshot/tuning.
2. loader/ABI — driver module, forwarder, model DLL, capability params.
3. feature lifecycle — create/release/rebuild/pending submission.
4. dispatch — única chamada ao modelo.
5. resources/state — scratch, ownership, resize e barriers.
6. exposure/HDR.
7. residual.
8. multipass/history.

## Implementação

- [x] Canonicalizar `HostDlssNr` como `D3D12NrExecutor`.
- [x] Manter `HostDlssNr` como alias de compatibilidade sem tocar `HostServer64`.
- [x] Separar loader, lifecycle e dispatch sem mudança semântica.
- [x] Manter uma única boundary de chamada ao modelo.
- [x] Preservar load policy do driver/forwarder/model.
- [x] Preservar `JustBuilt()` durante o primeiro split.
- [ ] Portar pending-submission/epoch maduro.
- [ ] Extrair resource/state map por owner/lifetime.
- [ ] Portar scale/subrect/padding.
- [ ] Portar pre/post-SR/RR/history e multipass.
- [ ] Portar HDR/exposure/residual.
- [ ] Substituir toda dependência OptiScaler Config/State por snapshot standalone.

## Revisão obrigatória

- [x] Primeiro split segue ownership: loader/lifecycle/dispatch.
- [x] Driver/forwarder/model mantêm a load policy anterior.
- [x] Nenhuma otimização funcional escondida no primeiro split.
- [x] Nenhuma interface virtual/heap/lock adicionada para dividir arquivos.
- [ ] Cada resource possui owner/create/state/release/resize/failure.
- [ ] Cada barrier possui estado anterior/próximo/caller guarantee.

## Validação rápida

- [x] Seis métodos comparados corpo-a-corpo com o seed anterior.
- [x] Host64 compila sem mudança de callsite.
- [x] Portable Core validation PASS.
- [x] Windows integrated validation + 19/19 CTest PASS.
- [ ] Teste/fake do lifecycle create/rebuild/pending.
- [ ] Resize/rebuild com substitutes.
- [ ] Comparar host CPU before/after quando hot helpers cruzarem TUs.
- [ ] Checker <=300 em todo executor extraído.

## Gate

- [x] Seed standalone não depende diretamente de OptiScaler.
- [x] Uma call boundary DLSS-NR preservada no seed.
- [ ] Resource/state map completo.
- [ ] Semântica madura de pending/rebuild/multipass/HDR/residual portada.
- [x] Zero arquivo handwritten >300 no executor extraído atual.

## Próxima ação

Portar pending-submission/epoch como boundary pequeno e testável antes de tocar resources/state.

## Próxima fase

Fase 06 somente após fechamento e revisão da Fase 05.
