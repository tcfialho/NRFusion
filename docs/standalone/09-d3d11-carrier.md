# Fase 09 — D3D11 x64 bridge/carrier

## Objetivo

Qualificar D3D11→D3D12 sem carregar os monólitos atuais para a nova arquitetura.

## Dependências

Fases 07–08.

## Fora de escopo

- x86
- CPU frame transport

## Implementação

- [x] Partir de `SyntheticDx11BridgeProvider` e testes existentes.
- [x] Separar hook/acquisition D3D11 do bridge D3D11→D3D12.
- [x] `CaptureD3D11.cpp` não foi reutilizado; o hook x64 ficou em owner novo e pequeno.
- [x] Separar acquisition, shared-resource sync, bridge e compose por ownership.
- [x] Definir slot/fence reuse.
- [x] Capturar color; guides só com provenance válida.
- [x] Tratar resize/device/context destruction.
- [x] Cada source/header <=300 linhas.

## Revisão obrigatória

- [x] Bridge funcional não prova Acquire.
- [x] Keyed mutex/fence ordering.
- [x] Slot só reutiliza após retirement.
- [x] Cada full-frame copy é identificada.
- [x] Split não duplica device/context ownership.

## Validação rápida

- [x] Teste atual antes/depois do split.
- [x] Hook/acquisition controlado no harness.
- [x] Stress in-flight/resize/reset.
- [x] LOC checker.

## Gate

- [x] Acquire e bridge demonstrados.
- [x] Transporte GPU-resident.
- [x] Nenhum arquivo tocado do carrier >300 linhas.

## Próxima fase

Fase 10.

## Operacional da fase

- Desenvolvimento normal, commits e revisão: GitHub connector em standalone/integration.
- Primeiro gate: portable CI automático em Ubuntu quando aplicável.
- Segundo gate: Windows hosted fast CI automático para compilação/testes que não exigem hardware físico.
- Notebook Windows: somente gate físico final de hook/acquisition D3D11 x64, ordering de sync e transporte GPU-resident.
- Falha no gate físico gera log; a correção volta ao GitHub e ao CI antes de repetir o hardware gate.
- Não editar o código no notebook durante o loop normal da fase.

## Fechamento

- Hosted portable e Windows fast: PASS no código validado.
- Gate físico x64 em hardware real: Acquire PASS, bridge D3D11→D3D12 PASS e hook x64 controlado PASS.
- O hook legado de `CaptureD3D11.cpp` permanece x86 por design e não foi expandido.
- `SyntheticDx11BridgeProvider.cpp` foi dividido por ownership; provider e resources ficaram abaixo do soft limit.
- O caminho normal usa shared resources + fences GPU; não há transporte full-frame por CPU.
- Código validado: `25ee4835d4e45bb446f48e8d640d6ce1b6aaa777`.
