# Fase 19 — histórico de subgates

Este arquivo preserva a trilha resumida de evidência da Phase 19.
O estado normativo e o ledger final ficam em `19-vram-resources.md`.

## 19a — scratch opcional

`fe3a595` adicionou usage explícito e retirement de scratch opcional:
PassScratch segue passes efetivos; ColorSmall segue reduced; ActiveColor segue crop;
residual segue across-RR. Retirement continua deferred/fail-closed.

Validação: Portable `36324845068`, Focused `36324845106`, Windows `36324845150` PASS.
Artifact `10933437248`, sha256
`81bc41d6ea7ad43586fea26b91db1072e2445267c0ddb889a78a0af637d8628e`.

## 19b — accounting scratch/guides

`c050a50` adicionou count/bytes ativos; `c1f8052` corrigiu MSVC;
`7b30eaf` contabilizou bytes aposentados; `e945699` cobriu guide clones;
`98931f0` aposentou clone stale quando o guide volta a ser direto.

Validação final do lote: Portable `36352523502`, Focused `36352523438`,
Windows `36352523339` PASS. Artifact `10942138379`, sha256
`e10d18105ea991bb311265859ab85a51b4ab1f16c3908ce6c8d88e456a82df12`.

## 19c — codec

`ea67c56` consolidou 48 heaps shader-visible em um heap de 384 descriptors.
Os 48 constant buffers de 256 B continuam separados para segurança in-flight.
Accounting: 48 resources / 12 KiB / 1 heap após init; zero após shutdown.

## 19d — diagnostics

`145951f` tornou query/readback temporários e liberados após fence/read terminal;
falhas de prepare publicam somente o par completo.

Portable `36353342312`, Focused `36353342318`, Windows `36353342324` PASS.
Artifact `10942253939`, sha256
`73bc01564241712f427ef6a58e3e87bb10381639c5e70869432132098284739e`.

## 19e — synthetic D3D12

`a140aef` adicionou accounting sem GPU work/device query.
Scale gate: 6 resources, 122880 bytes, 1 heap após dois slots; zero após shutdown.

Portable `36353705045`, Focused `36353705106`, Windows `36353705052` PASS.
Artifact `10943172628`, sha256
`ec0528af05b88de79511f4a8970fac3599b483839b876848cfade5dd8acbdf42`.

## 19f — zero guides Host64

`c9504e9` aposenta staging após guide fence.
`8884173` aposenta depth/motion zero quando ambos os imports substituem fallback e
os fences de criação/último uso concluíram.

`c9504e9`: Portable `36354867650`, Focused `36354867628`, Windows `36354867658` PASS.
Artifact `10943502007`, sha256
`7661fdbef41a1ca551b01a01189e3c3b9fca7e581cb928edd829fd2efd52c7f1`.

`8884173`: Portable `36355137406`, Focused `36355137407`, Windows `36355137413` PASS.
Artifact `10943254380`, sha256
`c736148bc6d8dfc6f0c093d6c3b3ffca7633ed7f2eb383b198077a87af4eec60`.

## 19g — Stop Host64

`8d1d3ee` adicionou idle marker de shutdown e libera owners somente após conclusão real.
Timeout/signal failure preserva recursos em voo. Stop é idempotente.

Portable `36357287344`, Focused `36357287338`, Windows `36357287300` PASS.
Artifact `10943849947`, sha256
`7adb6e5f21a1bf03820b0b3a83058e91e777c6f3d22a5c405f40e47c6337ac6e`.
RTX 4050: `host64_stop_cleanup_tests` PASS em 0,62 s.

## 19h — init Host64 + peak VRAM

`8c8812e` publicou init D3D12 Host64 atomicamente.
Portable `36360933948`, Focused `36360933832`, Windows `36360934021` PASS.
Artifact `10945422479`, sha256
`f84305d849171ced41cdc13d05811d9e8da91fde98b39dc83eca7c0ccf38e824`.

Primeira medição dedicada por PID: baseline `6f597f4` e `8c8812e`,
92,254 MiB mediana/peak observado em 3/3 por lado.

## 19i — synthetic failure paths

`0fe7ceb` removeu definições D3D11 duplicadas/LNK4006, tornou ring/bootstrap D3D11
atômicos e limpou failures de init no Synthetic D3D12.

Portable `36362975823`, Focused `36362975826`, Windows `36362975825` PASS.
Artifact `10946427117`, sha256
`cec7563559eb6d5ecdf4aa31fa9dc993d71228f230509b483d3cee685c6c4968`.
RTX 4050: `synthetic_dx11_bridge_test` PASS em 0,85 s.

## 19j — OpenGL bootstrap

`f76db8a` tornou device/queue/allocators/fences/handles do bootstrap D3D12 OpenGL atômicos
e centralizou cleanup de init/shutdown.

Portable `36363371278`, Focused `36363371274`, Windows `36363371295` PASS.
Artifact `10947080021`, sha256
`c7d1c0711f5a29c1b2f14df72503fb288524ab22a7b7ddde292927308ef96544`.

Hosted: synthetic OpenGL e state-restore PASS; external interop SKIP.
RTX A/B: raw recreation/reuse passam; provider-cycle falha igualmente em `0fe7ceb`
e `f76db8a`, logo não é regressão do ownership patch.

## 19k — Capture32 ownership e depth

`12d3045` iniciou split estrutural; a primeira versão compilou, mas Focused rejeitou
`CaptureD3D11.cpp` ainda >300. Nenhum novo code batch foi aberto até Windows PASS.

`b55c668` concluiu split exato byte-a-byte do source anterior:
principal 93 linhas; fragments de resources/depth/transport/session/hook setup/runtime
todos <=291. Portable `36366356794`, Focused `36366356812`,
Windows `36366356784` PASS.
Artifact `10947054968`, sha256
`6087576b7866a3ade198ffb1f24ef03adc795498300a64419b7923489c4b59b3`.

`e132c6e` tornou o depth opcional atômico: SRV, shared texture, keyed mutex, shared handle,
UAV, shader e constant buffer ficam locais até sucesso; falha fecha handle e destrói COMs.

Portable `36366804431`, Focused `36366804475`, Windows `36366804444` PASS.
Artifact `10947318107`, sha256
`8eaab2d813847326200debb8e7fd1cc964a81d9de9e16af72c6c87798e1baeae`.

Medição final no mesmo RTX 4050 e workload: baseline `6f597f4` e code head `e132c6e`
ficaram em 92,254 MiB de Dedicated Usage em 3/3 por lado; delta 0 MiB.

## Resultado

Todos os critérios normativos de `19-vram-resources.md` foram fechados.
Phase 19 CLOSED; próxima fase: 20.
