# Fase 05 — Executor DLSS 5 D3D12 canônico

## Status

**Em andamento.**

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

`HostDlssNr` já possui:

- policy de load do driver NGX;
- forwarder `nvngx.dll_dlssnr.dll`;
- model DLL `nvngx_dlssnr.dll`;
- capability params;
- create/release da feature primária;
- pending-submission simplificado via `JustBuilt()`;
- uma boundary `dlssnr_call_evaluate_v2`;
- tuning básico sem `Config/State`.

Limites:

- nome/ownership ainda são específicos do Host64;
- loader, lifecycle e dispatch vivem no mesmo arquivo;
- não possui resource/state map maduro;
- não possui multipass por feature;
- não possui HDR/exposure/residual equivalentes ao executor maduro.

### Executor maduro de referência

`tests/fixture/OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp` possui ~3883 linhas e permanece
read-only. A revisão mapeou os seguintes boundaries:

- loader/forwarder/capability params: ~521–732;
- retirement/rebuild/resources: ~733–820 e ~1815–2200;
- state/barriers/format: ~1177–1413;
- primary/multipass create + pending submission: ~1815–2185;
- scale/subrect/guides/encode/dispatch: ~2200–2720;
- resolve/HDR/residual: ~2720–3120;
- public pre/post SR/RR seams: ~3128–3539;
- shutdown/lifetime: ~3705–3755.

O fixture não será copiado nem substantivamente modificado.

## Boundaries alvo

1. `D3D12NrExecutor` — API pública e snapshot/tuning.
2. loader/ABI — driver module, forwarder, model DLL, capability params.
3. feature lifecycle — create/release/rebuild/pending submission.
4. dispatch — única chamada ao modelo.
5. resources/state — scratch, ownership, resize e barriers.
6. exposure/HDR.
7. residual.
8. multipass/history.

`HostDlssNr` vira somente compatibilidade para o host existente, sem tocar `HostServer64`
durante o split mecânico.

## Implementação

- [ ] Extrair mecanicamente `HostDlssNr` para `D3D12NrExecutor`.
- [ ] Separar loader, lifecycle e dispatch sem mudança semântica.
- [ ] Manter uma única boundary de chamada ao modelo.
- [ ] Preservar load policy do driver/forwarder/model.
- [ ] Preservar `JustBuilt()` durante o primeiro split.
- [ ] Depois portar pending-submission/epoch maduro.
- [ ] Extrair resource/state map por owner/lifetime.
- [ ] Portar scale/subrect/padding.
- [ ] Portar pre/post-SR/RR/history e multipass.
- [ ] Portar HDR/exposure/residual.
- [ ] Substituir toda dependência OptiScaler Config/State por snapshot standalone.

## Revisão obrigatória

- [ ] Split segue ownership/lifetime.
- [ ] Cada resource possui owner/create/state/release/resize/failure.
- [ ] Cada barrier possui estado anterior/próximo/caller guarantee.
- [ ] Driver/forwarder/model têm load policy explícita.
- [ ] Nenhuma otimização funcional escondida no split.
- [ ] Nenhuma interface virtual/heap/lock adicionada só para dividir arquivos.

## Validação rápida

- [ ] Diff mecânico do seed standalone.
- [ ] Host64 continua compilando sem mudança de callsite.
- [ ] Teste/fake do lifecycle create/rebuild/pending.
- [ ] Resize/rebuild com substitutes.
- [ ] Checker <=300 em todo executor extraído.
- [ ] Comparar host CPU antes/depois se hot helpers cruzarem TUs.
- [ ] Modelo real fica para gate de hardware.

## Gate

- [ ] Sem OptiScaler direto.
- [ ] Uma call boundary DLSS-NR.
- [ ] Resource/state map completo.
- [ ] Zero arquivo handwritten >300 no executor extraído.

## Próxima fase

Fase 06 somente após fechamento e revisão da Fase 05.
