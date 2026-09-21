# Fase 01 — Evidência do FrameContract

## Mudança estrutural

Os tipos de frame saíram de `Types.hpp` para `include/nrfusion/FrameContract.hpp`.

Linhas no corte final:

- `FrameContract.hpp`: 222;
- `Types.hpp`: 95;
- `FrameContractProvider.hpp`: 32;
- `frame_contract_tests.cpp`: 182;
- `PipelinePolicy.cpp`: 50;
- `CMakeLists.txt`: 267 (inalterado pela fase após code review).

Nenhum arquivo novo/tocado excede 300 linhas. A primeira implementação acrescentava 9 linhas ao CMake
já acima do soft target; a revisão removeu esse crescimento. A integração CTest fica para a modularização
da Fase 02. O teste da fase é header-only e compila sem linkar `nrfusion_core`.

## Contrato de recurso

`ResourceRef` preserva os três primeiros campos legados:

```text
opaqueId | resolution | format
```

e acrescenta metadata trailing/default:

```text
provenance
reliability
ownership
lifetime
sourceFrameId
```

Isso mantém os inicializadores agregados antigos de três campos válidos.

### Provenance

- `GameNative`
- `DlssContract`
- `OpticalFlow`
- `ShaderEstimated`
- `Generated`
- `Unknown`

Motion usa a provenance explícita como fonte autoritativa. Assim, um resource marcado `Generated`
não pode ser reinterpretado como Native apenas porque um flag legado diz Native.

### Reliability

`Unknown / Unreliable / Reliable`.

Resource presence e reliability são independentes. Metadata explícita sempre prevalece sobre os
booleans legados de depth/motion.

### Ownership / lifetime

Ownership:

- Borrowed;
- ProviderOwned;
- Shared;
- Unknown.

Lifetime:

- Frame;
- UntilNextAcquire;
- Session;
- Unknown.

Metadata nova é transicionalmente binária:

- tudo `Unknown` + `sourceFrameId=0`: contrato legado aceito;
- metadata explícita: provenance/reliability/ownership/lifetime precisam estar completos;
- qualquer metadata explícita exige `sourceFrameId != 0`; `lifetime` descreve a vida do objeto GPU,
  enquanto `sourceFrameId` descreve o frame do conteúdo;
- combinação parcial falha fechada.

## Contrato de frame

Obrigatório para `ReadyForCore()`:

- frame id não-zero;
- API conhecida;
- color válido;
- render/output dimensions válidas;
- color resolution igual à render resolution;
- jitter finito;
- nenhum resource válido explicitamente associado a outro frame;
- metadata, quando usada, bem formada;
- color não explicitamente Unreliable.

Opcionais:

- depth;
- motion vectors;
- exposure;
- reactive mask.

Identidade adicional:

- `hostFrameToken`;
- `viewId`;
- `configurationGeneration`;
- `resetHistory` separado de `cameraCut`.

O work id continua pertencendo a `WorkTicket/WorkLedger`; ele não foi duplicado no frame contract.

## Compatibilidade transitória

`depthReliable`, `motionVectorSource` e `motionVectorsReliable` permanecem temporariamente para
não modificar `apply_to_optiscaler.py` de 3900 linhas nesta fase.

Regras:

- metadata explícita vence os campos legados;
- metadata parcial nunca pode retornar reliability/motion utilizável para policy;
- metadata ausente preserva o comportamento antigo;
- o core `PipelinePolicy` já usa `DepthReliable()` e `MotionReliable()`, não acessa o boolean
  de depth diretamente;
- esses aliases são dívida transitória e deixam de ser necessários quando os providers/host forem
  migrados nas fases próprias.

## IPC x86

O contrato não inclui handles Windows, COM, VkImage ou ponteiros.

`IpcProtocol.hpp` já usa inteiros de largura fixa para session/work/feature/view/fence e mantém
jitter/reset/camera-cut no payload. A serialização efetiva de source frame/config generation será
tratada na Fase 10, sem mudar o contrato portátil.

`ResourceRef` e `FrameContext` foram validados como:

- trivially-copyable;
- standard-layout.

Isso permite construir snapshots/IPC explícitos sem transformar o core em API-specific.

## Validação

Teste dedicado:

`tests/frame_contract_tests.cpp`

Cobre:

- contrato válido;
- metadata parcial fail-closed;
- color/depth associados ao frame futuro;
- reliability explícita vencendo flag legado;
- metadata parcial não influencia `DepthReliable`/`MotionReliable`;
- provenance explícita vencendo source legado;
- compatibilidade do contrato legado;
- fake provider válido/incompleto/contraditório;
- propagação de host token/view/config generation;
- 100.000 frames alternando GameNative/DlssContract.

Validação local executada:

```text
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror
frame_contract_tests: PASS
PipelinePolicy.cpp compile: PASS
```

Não foi disparado CI completo. O ambiente continua sem checkout local completo do NRFusion; a
compilação local usou o boundary portátil exato relevante para esta fase.

## Limites deliberados

Esta fase define o contrato; não migra todos os carriers reais.

Providers antigos com metadata ausente continuam no modo legado e **não** contam como prova de
provenance/lifetime qualificada. Cada carrier passa a preencher metadata explícita na sua fase de
Acquire/Normalize.

## Gate Fase 01

Fechado após code review. Próxima fase: standalone runtime shell.
