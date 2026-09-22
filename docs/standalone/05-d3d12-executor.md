# Fase 05 — Executor DLSS 5 D3D12 canônico

## Status

**Em andamento.** Auditoria confirmou que o gate ainda não fecha. Subgates 01–03b preservados; 03c frame planning standalone concluído.
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
- [x] Portar pending-submission/epoch maduro.
- [ ] Extrair resource/state map por owner/lifetime. Deferred retirement de feature já portado.
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
- [x] Teste/fake do lifecycle pending por epoch.
- [ ] Resize/rebuild com substitutes.
- [ ] Comparar host CPU before/after quando hot helpers cruzarem TUs.
- [ ] Checker <=300 em todo executor extraído.

## Gate

- [x] Seed standalone não depende diretamente de OptiScaler.
- [x] Uma call boundary DLSS-NR preservada no seed.
- [ ] Resource/state map completo.
- [ ] Semântica madura de pending/rebuild/multipass/HDR/residual portada.
- [x] Zero arquivo handwritten >300 no executor extraído atual.

## Subgate 02 — submission epoch

- `NrSubmissionGate` portátil, sem heap/lock.
- Create em epoch N marca pending.
- Evaluate em epoch <= N falha fechado.
- Primeiro epoch > N libera a feature.
- `EvaluateForEpoch()` delega à única boundary `Evaluate()`.
- `Evaluate()` direto também rejeita enquanto o gate estiver pending.
- Host64 legado permanece sem epoch e sem alteração de callsite.
- Portable 7/7 PASS; Windows 20/20 PASS.

## Subgate 03a — deferred retirement

- Queue fixa de 64 slots, sem heap/lock.
- Delay padrão de 32 calls, igual ao executor maduro.
- Rebuild de feature estaciona a feature anterior em vez de liberar imediatamente.
- Overflow falha fechado e preserva o ponteiro ativo.
- Shutdown drena o backlog sob a garantia já existente de GPU idle do caller.
- Owner já aceita Resource; wiring de surfaces fica para o próximo subgate.
- 100.000 ciclos no teste portátil com 0 allocations.
- Portable 8/8 PASS; Windows 21/21 PASS.

## Subgate 03b — scratch owner inicial

- Owner explícito para `output`, `colorCopy` e `hdrCopy`.
- Criação preserva UAV + dimensões work/frame do executor maduro.
- Resize estaciona recursos antigos na mesma retirement queue.
- Estado esperado é validado antes de emitir barrier.
- Enum inválido falha fechado; não cai silenciosamente em `hdrCopy`.
- Regressão WARP cobre create/idempotência/barrier/resize/retire.
- Target focado não depende mais de `nrfusion_core`: compila 2 fontes de produção + 1 teste.
- Nenhum full build/CI foi disparado para este subgate.

## Resource-state map auditado para o próximo subgate

- `output/passScratch`: repouso UAV; NPSR apenas enquanto alimentam o próximo pass/resolve.
- `colorCopy`: UAV → NPSR após encode; retorna UAV no final do frame.
- `hdrCopy`: UAV → NPSR; usa COPY_SOURCE temporário; retorna UAV.
- `colorSmall/outputNative`: UAV ↔ NPSR por uso.
- guide clone: COPY_DEST → NPSR para evaluate; volta COPY_DEST para o próximo copy.

## Próxima ação

Validar scratch + frame-plan em Windows fast; depois expandir o owner de surfaces e extrair o seam encode/resolve.

## Próxima fase

Fase 06 somente após fechamento e revisão da Fase 05.


## Subgate 03c — frame planning standalone

- Config puro de frame planning para working scale, passes/unlock e proxy backend.
- Working scale preserva a semântica madura: NaN -> 1.0, clamp 0.25..2.0 e arredondamento +0.5.
- Passes preservam 1..3 por padrão, 1..30 destravado e proxy backend força 1.
- Subrects de color/depth/motion são validados contra as surfaces antes de qualquer uso.
- Padding/crop pre-SR é explícito no plan; motion scale acompanha work/native.
- Boundary é portátil e não depende de Config/State/OptiScaler.
- Teste isolado C++20 com -Wall -Wextra -Wpedantic -Werror: PASS.

### Auditoria do gate após 03c

A Fase 05 ainda não pode ser marcada concluída. Faltam integração GPU real de encode/resolve,
expansão completa do resource/state owner, features multipass por epoch, HDR/exposure/residual e
os seams pre/post SR/RR. Esses itens existem hoje somente no fixture maduro e não devem ser
copiados com dependências de Config/State.


## Subgates restantes para fechar a Fase 05

1. **03d resources/state completos** — integrar `passScratch`, `colorSmall`, `outputNative`,
   `activeColor`, residual surfaces e guide clones com owner/state/resize/failure explícitos.
2. **04 encode/resolve + scale real** — ligar o frame plan aos resources e portar codec/copy/resample
   sem dependência de `Config`/`State`.
3. **05 multipass/history** — features extras, create epoch por pass, ping-pong e reset/history.
4. **06 HDR/exposure/residual** — exposure source, HDR encode/resolve e Across-RR residual standalone.
5. **07 seams + snapshot final** — pre/post SR/RR, snapshot operacional completo e revisão adversarial final.

A Fase 06 do plano global continua bloqueada até esses cinco subgates fecharem.
