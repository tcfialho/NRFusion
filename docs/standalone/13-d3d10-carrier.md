# Fase 13 — D3D10 carrier

## Objetivo

Provar rota mínima D3D10 sem criar código grande antes de saber se o interop é viável.

## Dependências

Fases 07–08.

## Fora de escopo

- Executor NR D3D10
- CPU screenshot

## Implementação

- [x] Localizar Acquire seam/ownership D3D10/DXGI.
- [x] Criar proof-of-route GPU-resident em arquivo(s) pequenos.
- [ ] Definir color/depth/motion realmente acessíveis.
- [x] Bridge D3D12 + compose back demonstrado com identity/no-op D3D12.
- [x] Sync sem blocking recorrente no carrier; keyed acquire usa timeout 0.
- [ ] Só após prova, promover prototype a carrier modular.
- [x] Nenhum prototype vira arquivo >300 “temporariamente”.

## Revisão obrigatória

- [x] Cada full-frame copy justificada no route contract; runtime ainda pendente.
- [x] Sem sharing seguro = Blocked no route contract.
- [x] Guides não inferidos; proof atual é color-only e não anuncia depth/motion.
- [x] Prototype/route contract não duplica core policy.

## Validação rápida

- [x] Micro-harness Acquire+interop.
- [x] Steady/reuse e resource/resize recreation no hosted Windows.
- [ ] Medir sync/CPU no gate físico; copies já são contadas exatamente.
- [x] LOC checker.

## Gate

- [x] Rota demonstrável no hosted Windows; gate físico ainda pendente.
- [x] Nenhum arquivo >300 no proof/carrier.

## Próxima fase

Fase 14.


## Subgate 13a — route contract

Código validado: `e69731c`.

A auditoria não encontrou owner D3D10 existente; a API aparece apenas em
detecção/diagnóstico e continua unsupported.

Rota qualificada para o proof:
1. D3D10.1 copia a cor do jogo para um Texture2D RGBA16F criado com shared
   keyed mutex;
2. D3D11 abre o legacy DXGI shared handle e adquire o keyed mutex com timeout 0;
3. D3D11 copia GPU-side para um recurso D3D11.1 criado com
   `SHARED_NTHANDLE | SHARED_KEYEDMUTEX`;
4. D3D12 abre o NT handle;
5. compose-back faz o caminho inverso.

Motivo para o estágio D3D11 intermediário:
- o handle legado obtido via `IDXGIResource::GetSharedHandle` não é NT handle;
- o contrato D3D12 `OpenSharedHandle` usa NT handles;
- D3D11.1 fornece a conversão prática via novo recurso NT-shared, exigindo uma
  segunda cópia GPU.

Custo explícito do proof:
- 2 full-frame copies inbound;
- 2 full-frame copies outbound;
- nenhuma dessas cópias é escondida como "zero copy".

Sync:
- D3D10.1/D3D11: keyed mutex;
- `AcquireSync` deve usar timeout 0 e falhar por backpressure em vez de
  bloquear;
- D3D11/D3D12: reutilizar o fence bridge já existente.

Contrato portátil cobre:
- D3D10.1;
- same adapter;
- Texture2D RGBA16F;
- legacy shared surface;
- keyed mutex + timeout zero;
- D3D11 legacy open;
- D3D11 NT share;
- D3D12 NT import;
- copies GPU inbound/outbound;
- compose-back GPU.

Validação:
- focused portable `36078628140`: PASS;
- Windows hosted `36078628160`: PASS;
- checkpoint:
  `nrfusion-source-e69731c9a52f56ebcafdb94a50e4659602fc04cb`;
- nenhum runtime D3D10 foi executado como evidência.

### Próximo subgate

Criar micro-harness Windows same-adapter D3D10.1/D3D11.1/D3D12 que prove
a cadeia de handles, keyed mutex timeout-0, ida/volta GPU e contagem das quatro
cópias antes de promover qualquer carrier.


## Subgate 13b — micro-harness cross-API

Código validado: `8a23d4c`.

Harness:
- mesmo adapter DXGI não-software para D3D10.1, D3D11.1 e D3D12;
- D3D10 legacy shared surfaces com keyed mutex;
- D3D11 abre os handles legados;
- D3D11.1 cria a ponte NT com
  `SHARED_NTHANDLE | SHARED_KEYEDMUTEX`;
- D3D12 abre o NT handle;
- D3D11/D3D12 reutiliza `D3D11D3D12FenceBridge`;
- todos os `AcquireSync` do carrier usam timeout zero e só `S_OK` conta
  como aquisição bem-sucedida.

Roundtrip identity:
1. D3D10 source -> legacy input;
2. D3D11 legacy input -> NT bridge;
3. D3D12 participa do input/output fence ordering sem mutação de dados;
4. D3D11 NT bridge -> legacy output;
5. D3D10 legacy output -> destination.

A contagem do carrier é exatamente quatro full-frame GPU copies por frame.
A cópia adicional para staging + `Map` existe somente para validação do
payload no harness e não entra nessa contagem.

Cobertura executada:
- 64 reuse cycles;
- 16 recreation/resize cycles;
- payload RGBA16F validado após ida/volta;
- 320 carrier full-frame copies esperadas e verificadas.

Histórico de validação:
- `36080242669`: build PASS, runtime FAIL;
- `36080655085`: diagnóstico isolou criação NT sem keyed flag;
- `36081014555`: chegou ao payload e revelou ownership NT incompleto;
- `36081364306`: portable PASS + source-size PASS;
- `36081364289`: Windows hosted PASS;
- `nrfusion_d3d10_external_bridge_tests`: **Passed**, não SKIP;
- checkpoint:
  `nrfusion-source-8a23d4c3a3879915fe24b575ab524e2a7c810b63`.

Gate físico preparado:
`tools/validate_phase13_hardware.ps1`

Ele executa o binário diretamente com
`NRFUSION_TEST_D3D10_HARDWARE=1`; exit 77 não é aceito como PASS.

### Estado

A Fase 13 continua **IN PROGRESS**.

A rota foi demonstrada em runtime hosted. Faltam:
- execução no GPU/driver físico;
- medição explícita de sync/CPU no gate físico, mantendo o readback de validação
  fora do custo steady-state.

Se ambos passarem, a Fase 13 pode ser encerrada.
