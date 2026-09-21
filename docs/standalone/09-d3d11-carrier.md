# Fase 09 — D3D11 x64 bridge/carrier

## Objetivo

Qualificar D3D11→D3D12 sem carregar os monólitos atuais para a nova arquitetura.

## Dependências

Fases 07–08.

## Fora de escopo

- x86
- CPU frame transport

## Implementação

- [ ] Partir de `SyntheticDx11BridgeProvider` e testes existentes.
- [ ] Separar hook/acquisition D3D11 do bridge D3D11→D3D12.
- [ ] Se `CaptureD3D11.cpp` for reutilizado, fazer split mecânico antes de evolução funcional.
- [ ] Separar acquisition, shared-resource sync, bridge e compose por ownership.
- [ ] Definir slot/fence reuse.
- [ ] Capturar color; guides só com provenance válida.
- [ ] Tratar resize/device/context destruction.
- [ ] Cada source/header <=300 linhas.

## Revisão obrigatória

- [ ] Bridge funcional não prova Acquire.
- [ ] Keyed mutex/fence ordering.
- [ ] Slot só reutiliza após retirement.
- [ ] Cada full-frame copy é identificada.
- [ ] Split não duplica device/context ownership.

## Validação rápida

- [ ] Teste atual antes/depois do split.
- [ ] Hook/acquisition controlado no harness.
- [ ] Stress in-flight/resize/reset.
- [ ] LOC checker.

## Gate

- [ ] Acquire e bridge demonstrados.
- [ ] Transporte GPU-resident.
- [ ] Nenhum arquivo tocado do carrier >300 linhas.

## Próxima fase

Fase 10.
