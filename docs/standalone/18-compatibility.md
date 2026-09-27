# Fase 18 — State restoration e compatibilidade

## Objetivo

Isolar compatibility exceptions em módulos pequenos por causa/estado, sem framework monolítico.

## Dependências

Carriers principais.

## Fora de escopo

- Copiar hooks genéricos do OptiScaler.
- Tracking global default.
- Criar profile por jogo para mascarar leak geral.

## Auditoria

- O caminho DLSS NR em D3D12 pode substituir bindings do command list.
- `tools/requiem_game/main.cpp` já restaura manualmente PSO, graphics root signature,
  descriptor heaps, root table/constants, OM/raster/IA depois da avaliação.
- `D3D12NrExecutor` restaura resource states, mas não consegue consultar bindings anteriores:
  D3D12 não oferece getters para root signatures, PSO ou root arguments.
- `D3D12CarrierExecutor` recebe command list emprestado e precisa de estado fornecido pelo
  caller quando esse caller pretende continuar gravando na mesma lista.
- O `D3D12CarrierExecutor` ainda não tem caller de frame na branch; hoje existem
  bootstrap/contract/session/plan, mas nenhum callsite integrado de `Execute()` auditado.
- O synthetic D3D12 do patcher usa `SyntheticCommandContext::list`, command list privado;
  o clobber interno do provider não é leak ativo do command list do jogo nesse caminho.
- `CompatibilityDatabase.cpp` (350 linhas) e `ProfileStore.cpp` (666 linhas) ficaram
  intocados. Nenhuma exception por jogo foi adicionada.
- OpenGL standalone criava/importava textures no contexto WGL atual e terminava com
  `GL_TEXTURE_BINDING_2D = 0`; `f0ea299` preserva/restaura o binding anterior via RAII.
- D3D11 x64 standalone usa apenas CopyResource/fences nos owners auditados; não grava bindings.
- D3D10/D3D9 owners auditados são route/acquire/sync; não gravam pipeline bindings.
- Vulkan usa estado explícito de command buffer e não precisa de tracker implícito equivalente.
- `CaptureD3D11.cpp` x86 legado usa o immediate context e limpa bindings CS para null
  depois dos dispatches. Isso permanece dívida de state preservation do hook legado; Fases 09/10
  explicitamente não reutilizam esse monólito no carrier standalone. O arquivo tem 1302 linhas e
  exige split mecânico antes de evolução funcional.

## Implementação

- [x] Identificar states necessários para o boundary D3D12 auditado.
- [x] Separar state restore de profile matching/database.
- [x] Restore opt-in de descriptor heaps, graphics/compute root signature e PSO.
- [x] Root params/estado adicional usam callback caller-owned, sem alocação.
- [x] Nenhum map/mutex global no fast path.
- [x] Default `commandStateRestore == nullptr` não chama helper nem API D3D12.
- [x] Restore roda depois de `ExecuteFrame()`, inclusive quando o executor retorna falha.
- [x] Config de restore inválida falha antes de emitir restore parcial.
- [x] OpenGL preserva `GL_TEXTURE_BINDING_2D` em criação/recriação e early-return.
- [ ] Ligar restore no caller real do `D3D12CarrierExecutor` quando o cutover existir.
- [ ] Remover restore manual do Requiem somente depois de migrar seu owner >300 linhas.
- [ ] Corrigir state preservation no hook D3D11 x86 somente depois do split do monólito.

## Split estrutural

- `src/SyntheticDx12Provider.cpp` legado continua congelado para a closure explícita do patcher.
- O standalone compila:
  - `SyntheticDx12ProviderLifecycle.cpp`
  - `SyntheticDx12ProviderSubmit.cpp`
  - `SyntheticDx12ProviderResidual.cpp`
- Todos os novos owners ficam <=300 linhas.

## Revisão obrigatória

- [x] Nenhum profile mascara o leak geral.
- [x] Restore é chamado após sucesso/falha do executor quando habilitado.
- [x] Exceção não impõe chamadas D3D12 no default.
- [x] Split por estado/ownership, não por game arbitrariamente.
- [ ] Integração do caller real ainda pendente.

## Validação rápida

- [x] Harness WARP clobbera heap/root signature/PSO/root table e prova restore por dispatch.
- [x] Restore inválido é rejeitado fail-closed.
- [x] Benchmark default vs compatibility existe no harness.
- [x] Harness OpenGL sem hardware prova restore normal e em early-return.
- [x] LOC checker.
- [ ] Harness integrado do futuro caller do carrier.

## Evidência

- `07dfe42`: split standalone do SyntheticDx12Provider; 3/3 CI PASS.
- `5d2ebda`: restore caller-owned no boundary do D3D12 carrier; 3/3 CI PASS.
- `1ab383b`: benchmark default vs compatibility; 3/3 CI PASS.
- `fb8e338`: fast path nulo sem chamada ao helper.
- `f0ea299`: preservação do texture binding OpenGL.
- OpenGL lote `f0ea299`: Portable `36324255420` PASS; Focused Portable
  `36324255411` PASS; Windows `36324255443` PASS.
- Checkpoint `nrfusion-source-f0ea2997422707f8ac89992dff60a018b538e01e`,
  artifact `10933321776`,
  sha256 `b15c0412ff1a4ebd8436b287aa22be724e0d96d3481e16c1d93632a3c4f68d58`.
- Portable `36323693523`: PASS.
- Focused Portable `36323693525`: PASS.
- Windows `36323693528`: PASS.
- Checkpoint `nrfusion-source-fb8e338c3aa5aea801ce333cff659ce69b674c60`,
  artifact `10933915115`,
  sha256 `e3b8be5f34c438f682ef9242d18ecc1710cd31d8f78f1447a0f6705c62e0659b`.

## Gate

- [x] Default não executa restore nem API D3D12 adicional.
- [x] Módulo novo de restore respeita <=300 linhas.
- [x] CompatibilityDatabase/ProfileStore oversized não foram tocados.
- [ ] Caller integrado do D3D12 carrier ainda não existe para fechar o cutover.

**Subfases state-restore primitive e route-state audit da Fase 18 CLOSED. Fase 18 permanece IN PROGRESS até o caller real do carrier usar o contrato ou o caminho ser explicitamente retirado.**

## Próxima ação

Auditar o próximo carrier standalone com command list/context realmente reutilizado pelo host antes de criar qualquer nova exception.
