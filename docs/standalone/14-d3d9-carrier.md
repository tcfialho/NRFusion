# Fase 14 — D3D9/D3D9Ex carrier

## Objetivo

Provar D3D9Ex e D3D9 clássico separadamente sem crescer um legacy carrier monolítico.

## Dependências

Fases 07–08.

## Fora de escopo

- CPU screenshots
- Segundo NR runtime

## Implementação

- [x] Mapear Present/Reset/lost-device/ownership para o proof Ex.
- [x] Proof-of-route D3D9Ex primeiro.
- [x] Avaliar classic separadamente: Blocked para carrier GPU-resident normal.
- [x] Definir color RGBA16F realmente capturável; depth/motion não são inferidos.
- [x] Bridge D3D11/D3D12 demonstrada; Host64/hook continua fora deste proof.
- [x] Compose back color-only + ResetEx demonstrados no hosted Windows.
- [x] Separar Ex/classic; todos os arquivos tocados <=300.

## Revisão obrigatória

- [x] D3D9Ex/classic têm qualificação separada.
- [x] Bridge não é tratada como Acquire; existe D3D9ExCarrierAcquire explícito.
- [x] Sem transporte GPU = Blocked; classic cai neste caso.
- [x] ResetEx tem ownership explícito e rebind de queries.
- [x] Proof usa A16B16G16R16F/RGBA16F sem conversão escondida.

## Validação rápida

- [x] Micro-harness Ex.
- [x] 16 ResetEx loops + 16 recreation/resize loops.
- [x] Classic encerrado como Blocked tecnicamente, sem CPU fallback normal.
- [x] LOC checker.

## Gate

- [ ] D3D9Ex ainda depende do gate físico; classic já está Blocked com motivo.
- [x] Nenhuma rota CPU vendida como normal.
- [x] Código D3D9 tocado <=300 por arquivo.

## Próxima fase

Fase 15.


## Subgate 14a — contrato Ex/classic

Código: `29dc41f`.

Qualificação separada:
- **D3D9 classic:** `Blocked` para carrier GPU-resident normal. Uma rota por
  memória do sistema não é aceita como caminho normal.
- **D3D9Ex:** elegível somente com WDDM, same-adapter, textura RGBA16F
  default-pool/single-mip/non-MSAA compartilhável, handoff não bloqueante,
  bridge D3D11 NT-share -> D3D12, compose-back GPU e ownership de reset.

O contrato Ex contabiliza 2 cópias full-frame inbound + 2 outbound.

## Subgate 14b — transport + sync

Código:
- `05e8447` — harness D3D9Ex/D3D11/D3D12;
- `caef792` — correção de compile;
- `f2c0b7d` — helper one-shot de event handoff.

Provas hosted:
- shared texture D3D9Ex abre em D3D11 no mesmo adapter;
- ponte D3D11 NT-shared abre em D3D12;
- event queries completam nas duas direções;
- helper não contém polling loop;
- bounded polling existe somente no harness;
- `ResetEx` invalida/recria as queries explicitamente;
- Windows `36084240714`: PASS, teste D3D9Ex executado, não SKIP.

## Subgate 14c — roundtrip color-only

Código: `a336473`.

Roundtrip:
1. D3D9 game source -> shared input;
2. D3D11 shared input -> NT bridge;
3. D3D12 participa do fence ordering sem mutação de dados;
4. D3D11 NT bridge -> shared output;
5. D3D9 shared output -> game destination.

Carrier accounting:
- 4 full-frame GPU copies por roundtrip;
- 64 steady + 16 ResetEx + 16 recreation = 96 roundtrips;
- 384 copies esperadas/verificadas.

Validação:
- portable `36085316003`: PASS;
- Windows `36085315884`: PASS;
- teste D3D9Ex: Passed, não SKIP.

## Subgate 14d — Acquire + gate físico

Código validado: `a6882cb`.

`D3D9ExCarrierAcquire`:
- exige identidade/configuration generation válidas;
- exige device + color surface do mesmo device;
- exige RGBA16F, default pool e non-MSAA;
- preserva jitter/HDR/camera-cut/reset-history;
- produz color `GameNative/Borrowed/Frame/Reliable`;
- não inventa depth nem motion.

Harness físico executa o Acquire antes dos roundtrips.

Validação final hosted:
- focused portable `36086039485`: PASS;
- Windows `36086039483`: PASS;
- `nrfusion_d3d9ex_share_tests`: Passed, não SKIP;
- checkpoint:
  `nrfusion-source-a6882cb2388eee0692dd768aaec36706b270c53c`.

Gate físico:
`tools/validate_phase14_hardware.ps1`

O script:
- configura x64;
- compila somente `nrfusion_d3d9ex_share_tests`;
- define `NRFUSION_TEST_D3D9EX_HARDWARE=1`;
- executa o binário diretamente;
- exit 77 não é aceito como PASS.

O script/parser foi validado no Windows hosted `36085665378`.

## Estado

A Fase 14 permanece **IN PROGRESS**.

- D3D9 classic: **Blocked** por ausência da rota GPU shared-surface exigida.
- D3D9Ex: hosted runtime + Acquire completos; gate físico ainda pendente.

Após PASS físico, registrar Ex como Qualified, classic como Blocked e fechar
explicitamente a Fase 14.
