# Fase 15 — Universal guide acquisition

## Objetivo

Selecionar os melhores guides sem concentrar toda policy em um resolver gigante.

## Dependências

Carriers relevantes implementados.

## Fora de escopo

- Inventar provenance
- Exigir guides nativos

## Implementação

- [x] Auditar/reusar PipelinePolicy, MotionVectorResolver, GuideValidation e NVOF.
- [x] Preservar prioridade Native→DLSS contract→NVOF→shader→Zero.
- [x] Centralizar depth reliability/exposure nos accessors de FrameContext existentes.
- [x] Separar motion selection, validation e generation por owners distintos.
- [x] Registrar source/reliability no FrameContract com binding explícito.
- [x] Invalidar history em mudança material de guide.
- [x] Manter matriz API/provider→guides.
- [x] Nenhum novo “GuideManager” multifunção >300 linhas.

## Revisão obrigatória

- [x] Non-null != reliable.
- [x] Zero é explícito.
- [x] Não duplicar hierarquia já testada.
- [x] Camera cut/reset prevalece.
- [x] Módulos de guide <=300 linhas.

## Validação rápida

- [x] Casos MotionVectorResolver existentes, incluindo NVOF > shader.
- [x] Fake sources/provenance + evidence incompleta/stale.
- [x] Carriers com guides parciais mapeados explicitamente.
- [x] LOC checker.

## Gate

- [x] Game sem DLSS tem estratégia explícita.
- [x] Fallback observável pela fonte motion selecionada/diagnostics.
- [x] Carrier não contém policy duplicada.
- [x] Arquivos tocados <=300 linhas.

## Próxima fase

Fase 16.


## Subgate 15a — seleção única de motion

Código: `5030b38`.

`MotionGuideSelection` é o único owner da hierarquia:

1. Native;
2. DLSS contract;
3. NVOF;
4. shader-estimated;
5. Zero.

`PipelinePolicy` e `MotionVectorResolver` chamam a mesma seleção. A divergência
anterior, na qual o resolver aceitava shader antes de NVOF, foi removida.

`cameraCut` e `resetHistory` prevalecem e selecionam Zero.

Validação:
- portable `36110092702`: PASS;
- Windows `36110092730`: PASS.

## Subgate 15b — reliability + history

Código: `d87cc23`.

Não foi criado wrapper novo para depth/exposure:
- `FrameContext::DepthReliable()` continua sendo o owner da evidência de depth;
- `FrameContext::ExposureReliable()` continua sendo o owner da evidência de exposure.

`GuideHistoryState` registra somente fatos materiais para compatibilidade
temporal:
- motion source + reliability;
- depth provenance + reliability;
- exposure provenance + reliability;
- reset transitório separado dos fatos persistentes.

`TemporalHistoryRegistry` cria nova history identity quando esses fatos mudam.
`cameraCut/resetHistory` força reset one-shot; ao voltar para o mesmo estado no
frame seguinte não ocorre um segundo reset artificial.

Validação:
- portable `36110707277`: PASS;
- Windows `36110707430`: PASS.

## Subgate 15c — binding de provenance/evidence

Código validado: `268d49b`.

`MotionGuideBinding` grava motion gerado/selecionado no `FrameContract` sem
inventar evidence:
- source determina apenas o provenance compatível;
- reliability, ownership, lifetime e sourceFrameId são obrigatórios para
  qualquer source não-Zero;
- provenance conflitante, evidence ausente e frame stale falham closed;
- Zero exige ausência de recurso e fica explícito como fallback.

Validação:
- focused portable `36111227431`: PASS;
- Windows hosted `36111227420`: PASS;
- source-size: PASS;
- checkpoint:
  `nrfusion-source-268d49bc592340edf894aca6d3c08a961acabfe3`.

## Matriz API/provider → guides

| Carrier atual | Guides explícitos no Acquire | Fallback universal |
|---|---|---|
| D3D12 | color + depth/motion/exposure/reactive opcionais com evidence | Native/DLSS → NVOF → shader com depth reliable → Zero |
| Vulkan | color + depth/motion/exposure/reactive opcionais com evidence | Native/DLSS → NVOF → shader com depth reliable → Zero |
| D3D11 | Acquire nativo atual é color-only | NVOF quando qualificado; shader somente se depth reliable surgir; senão Zero |
| OpenGL | Acquire atual é color-only | NVOF quando qualificado; shader somente com depth reliable; senão Zero |
| D3D10 | proof de bridge color-only; Acquire nativo ainda não existe | nenhuma guide nativa inferida; NVOF/Zero somente após Acquire qualificado |
| D3D9Ex | Acquire color-only | NVOF quando qualificado; shader somente com depth reliable; senão Zero |
| D3D9 classic | Blocked no carrier GPU-resident | não aplicável |

Exposure é opcional em todos os casos: ausência/non-null não vira reliability.
Nenhum carrier ganha depth/motion/exposure por inferência.

## Estado

**Fase 15 CLOSED.**

O gate é portátil/hosted; não existe dependência física específica desta fase.
Os gates físicos pendentes das Fases 11–14 permanecem inalterados.
