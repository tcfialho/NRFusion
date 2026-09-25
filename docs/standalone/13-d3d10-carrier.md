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
- [ ] Criar proof-of-route GPU-resident em arquivo(s) pequenos.
- [ ] Definir color/depth/motion realmente acessíveis.
- [ ] Bridge D3D12 + compose back.
- [ ] Sync sem blocking recorrente.
- [ ] Só após prova, promover prototype a carrier modular.
- [ ] Nenhum prototype vira arquivo >300 “temporariamente”.

## Revisão obrigatória

- [x] Cada full-frame copy justificada no route contract; runtime ainda pendente.
- [x] Sem sharing seguro = Blocked no route contract.
- [ ] Guides não inferidos.
- [x] Prototype/route contract não duplica core policy.

## Validação rápida

- [ ] Micro-harness Acquire+interop.
- [ ] Depois steady/resize.
- [ ] Medir copies/sync/CPU.
- [ ] LOC checker.

## Gate

- [ ] Rota demonstrável ou blocker técnico.
- [ ] Nenhum arquivo >300 no proof/carrier.

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
