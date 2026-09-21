# Fase 05 — Executor DLSS 5 D3D12 canônico

## Objetivo

Extrair o executor D3D12 maduro para ownership próprio preservando sua semântica e ABI já comprovada.

## Dependências

Fase 04.

## Fora de escopo

- Otimizar barriers/copies
- Trocar algoritmo visual
- Inventar uma segunda interface NGX

## Implementação

- [ ] Definir input/output explícitos do executor.
- [ ] Reusar `HostDlssNr` e o forwarder `nvngx.dll_dlssnr.dll` como referência da ABI/call sequence já funcional.
- [ ] Escolher uma única boundary de chamada ao modelo; não duplicar vtable/export declarations em novos subsistemas.
- [ ] Substituir OptiScaler Config/State por parâmetros/snapshot.
- [ ] Preservar feature pending-submission e rebuild.
- [ ] Preservar subrect/padding, depth/motion compatibility e resource states.
- [ ] Preservar pre/post-SR, RR, history, scale, multipass, HDR/exposure e residual.
- [ ] Preservar failure latches, precision rebuild e lazy allocation.

## Revisão obrigatória

- [ ] Para cada resource: owner/create/state/release/resize/failure.
- [ ] Para cada barrier: estado anterior/próximo e caller guarantee.
- [ ] Driver NGX module, forwarder e model DLL têm ownership/load policy explícita.
- [ ] Early return não muda lifetime/state silenciosamente.
- [ ] Workaround sem repro permanece até fase de otimização.

## Validação rápida

- [ ] Boundary compila/executa com substitutes controlados.
- [ ] Loop reset/resize/rebuild sem modelo real quando possível.
- [ ] Modelo real somente no gate de hardware.

## Gate

- [ ] Sem dependência direta de OptiScaler.
- [ ] Existe uma única call boundary DLSS-NR.
- [ ] Extração revisável por resource/state map.
- [ ] Nenhuma otimização funcional misturada.

## Próxima fase

Fase 06.
