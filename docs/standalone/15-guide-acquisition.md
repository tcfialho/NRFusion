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
- Zero é explícito: pode limpar motion sem recurso ou materializar um recurso Generated zero-motion com evidence completa.

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

## Hardening pós-revisão

A revisão posterior ao fechamento encontrou falhas de integração/contrato e a fase
foi revalidada antes de permanecer fechada.

Estado validado em `7364209`:
- selection/history contracts mantidos em headers já presentes na closure do host;
- patcher closure coberta pelo portable-core em `standalone/integration`;
- history conectado ao `FusionRuntime::ResolveAuto`;
- reset/camera-cut não causa double structural generation;
- history signature inclui generation/provenance/reliability/resolution/format;
- binder fail-closed limpa motion stale e valida evidence/format/generation;
- Zero materializado como recurso Generated é aceito;
- resolver valida provenance/frame/evidence e recebe `resetHistory`;
- validator limpa hysteresis em `resetHistory`;
- NVOF separa capability de guide-ready e o provider placeholder fica
  intencionalmente indisponível até existir executor real;
- D3D11/OpenGL não falham por ausência desse backend.

Validação final:
- Portable `36271983567`: PASS;
- Focused Portable `36271983548`: PASS;
- Windows `36271983572`: PASS.

## Estado

**Fase 15 CLOSED após hardening.**

O gate é portátil/hosted; não existe dependência física específica desta fase.
NVOF permanece fail-closed, não anunciado como guide pronto.
Os gates físicos pendentes das Fases 11–14 permanecem inalterados.

## Segunda revalidação adversarial

A revisão dos achados #12–#30 foi repetida contra o código efetivamente publicado.

Resultado:
- NVOF continua fail-closed: o placeholder não cria submission, não publica guide e
  não anuncia completion/backpressure fictícios;
- resolver, validator, binder e history já cobrem provenance/evidence, resetHistory,
  configuration generation, stale binding, Zero materializado, idempotência no mesmo
  frame e mudanças de resolução/formato;
- Windows CI executa os testes de guide da fase e portable-core roda em
  `standalone/integration`;
- o único bypass residual de capability foi removido: `caps.nvof` sozinho não
  seleciona mais NVOF. A policy exige `nvofGuideReady` e evidence confiável do
  frame;
- o host OptiScaler continua sem anunciar NVOF porque não existe executor NVOF real.
  Isso é fail-closed, não uma capability ausente a ser inventada;
- history está no caminho efetivo
  `OptiScalerAdapter::ResolveAuto -> FusionRuntime::ResolveAuto -> UpdateAutoGuideHistory`.
  `MotionGuideBinding` permanece o boundary obrigatório para guides produzidos
  pelo runtime/provider; o contrato legado de motion borrowed do host não é
  reinterpretado como guide provider-owned.

Preparação estrutural para a correção:
- `f14fdab` dividiu `controller_tests.cpp` em partes <=300 linhas mantendo um
  único executável de teste;
- Portable `36276740194`: PASS;
- Focused Portable `36276740185`: PASS;
- Windows `36276740231`: PASS.

Correção do bypass:
- código validado: `6b788c0`;
- Portable `36276992447`: PASS;
- Focused Portable `36276992629`: PASS;
- Windows `36276992418`: PASS;
- checkpoint: `nrfusion-source-6b788c07f0439fdbd1473faf6e74dbd3f4126400`,
  artifact `10916768824`,
  sha256 `a86f320286f653df541a7ce77c1532ceff40a844ebb021db9c6bb1363218c0b6`.

**Fase 15 CLOSED após a segunda revalidação.**

A implementação de optical flow real continua trabalho futuro e não autoriza
`caps.nvof=true` até existir dispatch, completion e guide evidence reais.

