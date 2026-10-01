# NRFusion — Fase 2A: evidence and handoff

Runtime pipeline validated through discovery, ABI, real capture, stock replay and one exact custom live replacement. Phase 2A gates are satisfied on the current local branch. No push or installer publication performed.

## Baseline

- Branch `standalone/integration`; Phase 2A baseline `6a4bc15d77148df2e77de8b5601f75a60f1bc069`; implementation HEAD before this final evidence commit `33b6821a99ae0cbcbedf3c2b4381d4f55abe8e1f`.
- RTX4050 Laptop, SM89; NVIDIA617.14; CUDA driver13040; toolkit13.4.59.
- NR310.8.0.0, runtime SHA256 `e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a`.
- Official executable `D:\Users\tcfialho\Documents\NRFusion\dist\RequiemGame\RequiemGame.exe` with rebuilt official proxy. 1920x1080 output, 1280x720 input, WorkingScale1, FP8, pre-SR, one pass.
- This is the official controlled testbed with the real NVIDIA NR runtime. A shipping-game installation/visual qualification was not performed.
- Existing user deletion of `AGENT.md` preserved and excluded from the staged checkpoint.

## A. Kernel inventory

NR dispatches through `NvAPI_D3D12_LaunchCuKernelChain`, not observed public `cuLaunchKernel`. No second CUDA launch hook introduced. Native handles are `NVDX_ObjectHandle`, not `CUfunction`; ordering uses D3D12 command lists/queues, not `CUstream`.

Two executions,240 measured frames each,156 launches/frame,37440 records/run, all chains single-kernel, zero drops. Named/module-hashed kernels explain100% of measured native chain GPU time. This is coverage of this observed dispatch path, not a claim that every driver-internal operation is visible. Event overhead was10.036% against OFF; four final images were identical. Nsight CUDA API tracing did not observe this private submission path, so independent per-kernel Nsight validation remains unavailable.

The leading launch identities below reach at least70%. Full identities and all rows are in `../phase2a/2026-09-30-inventory/inventory-1.json`; address identities are process-local.

| Kernel | Identity | calls/f | us/call | ms/f | Share% | Grid | Block | Shared |
|---|---|---:|---:|---:|---:|---|---|---:|
| cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8 | `83da80729d32d742` | 1.0 | 658.308 | 0.6583 | 6.73 | 168,96,1 | 32,1,1 | 0 |
| cc_tinlayout_fused_swin_8h_256_8_chained_fp8 | `0d9b13a8076e7b30` | 6.0 | 108.668 | 0.6520 | 6.66 | 11,7,1 | 32,8,1 | 0 |
| cc_tinlayout_fused_swin_8h_256_8_chained_fp8 | `41d0a663a6a51f0f` | 6.0 | 101.999 | 0.6120 | 6.25 | 11,6,1 | 32,8,1 | 0 |
| cc_split_swin_16h_ffwd_512_chained_fp8 | `dee2466b0c129848` | 15.0 | 36.211 | 0.5432 | 5.55 | 6,3,2 | 32,8,1 | 0 |
| cc_tinlayout_fused_post_block_swin_1h_32_fp8 | `cd34ba8ff9035f2d` | 1.0 | 530.253 | 0.5303 | 5.42 | 169,97,1 | 32,1,1 | 0 |
| cc_vit_1d_ffn_contract_chained_fp8 | `296e45da13d03ae9` | 8.0 | 60.012 | 0.4801 | 4.91 | 24,1,4 | 32,4,1 | 0 |
| cc_vit_1d_ffn_expand_chained_fp8 | `213d246de9f36542` | 7.0 | 62.497 | 0.4375 | 4.47 | 96,1,1 | 32,4,1 | 0 |
| cc_split_swin_16h_qkv_512_chained_fp8 | `88e102041dfefaca` | 8.0 | 50.934 | 0.4075 | 4.16 | 6,4,4 | 32,4,1 | 0 |
| cc_vit_1d_qkv_chained_fp8 | `936a527c50363f22` | 8.0 | 49.017 | 0.3921 | 4.01 | 48,1,2 | 32,4,1 | 0 |
| cc_split_swin_16h_qkv_512_chained_fp8 | `0795d4d001301a6b` | 8.0 | 43.675 | 0.3494 | 3.57 | 6,3,4 | 32,4,1 | 0 |
| cc_tinlayout_fused_swin_1h_32_1_chained_fp8 | `c3662e49d6b0f0ac` | 2.0 | 157.013 | 0.3140 | 3.21 | 85,48,1 | 32,1,1 | 0 |
| cc_tinlayout_fused_swin_1h_32_1_ds_wait_fp8 | `ee224283bb8d7a25` | 1.0 | 311.245 | 0.3112 | 3.18 | 84,49,1 | 32,1,1 | 0 |
| cc_tinlayout_fused_swin_1h_32_1_chained_fp8 | `a2d945395fcd704f` | 2.0 | 142.782 | 0.2856 | 2.92 | 85,49,1 | 32,1,1 | 0 |
| cc_split_swin_16h_ffwd_proj_512_chained_fp8 | `783216a48c23b42d` | 15.0 | 15.720 | 0.2358 | 2.41 | 12,3,1 | 32,4,1 | 0 |
| cc_vit_1d_projection_chained_fp8 | `3235d93d120988ce` | 7.0 | 33.034 | 0.2312 | 2.36 | 24,1,4 | 32,4,1 | 0 |
| cc_split_swin_16h_proj_512_chained_fp8 | `ba396615dd81e757` | 14.0 | 15.023 | 0.2103 | 2.15 | 12,3,1 | 32,8,1 | 0 |
| cc_tinlayout_fused_swin_2h_64_2_chained_fp8 | `c29fdceab268fe6d` | 2.0 | 101.274 | 0.2025 | 2.07 | 43,25,1 | 32,2,1 | 0 |

## B. Launch sequence

Sequence was identical across480 frames/two executions. Observed:

- `ffwd → ffwd_proj`,15 occurrences/frame.
- `qkv → attention → projection`,7 occurrences/frame.
- `projection → ffn_expand → ffn_contract`,7 occurrences/frame.

The association with FFN/Swin/attention families agrees with `tools/fusion_analysis.py`, but layer numbers remain inferred. Adjacency alone does not confirm data dependency. For the selected kernel, reads/writes and counter ordering are supported by module metadata/address expressions plus isolated replay; edges to surrounding kernels remain unconfirmed. Complete sequence evidence: `../phase2a/2026-09-30-sequence/sequence.json`.

## C. ABI

Selected: `cc_vit_1d_ffn_expand_chained_fp8`, approximately4.47% of profiled native neural time. Source module SHA256 `bdc0cafe89442d2fa64ab168905e5ebcfe4bb7592604d0b4b2fca2db063b7a2b`.

One opaque aggregate argument,72 bytes, alignment8; proven by PTX declaration and ELF parameter metadata. Semantic fields are a qualified contract for this exact module/configuration, not arbitrary argument guesses.

| Field offset | Bytes | Classification | Captured range | Role/evidence | Confidence |
|---:|---:|---|---:|---|---|
|0|8|D3D12 GPU address|294912|FP8 input; address mapping and exact replay|high|
|8|8|zero|—|unused in qualified configuration|bounded|
|16|8|D3D12 GPU address|1179648|FP8 output; writes and exact replay|high|
|24|8|D3D12 GPU address|4194304|FP8 weight matrix; tiled loads and impulse probe|high|
|32,40|8 each|zero|—|unused in qualified configuration|bounded|
|48|8|D3D12 GPU address|512|ready counters; first24 signed values required nonnegative|high|
|56|8|D3D12 GPU address|512|done counters; first96 written zero|high|
|64,68|4 each|unsigned scalar|—|width12,height24; M288,K1024,N4096|high|

CUDA pointer queries did not classify these GPU addresses as CUDA allocations. Resource-creation observations positively mapped them to D3D12 allocations. Pools were97484288 and147719680 bytes; entire pools were not dumped. Launch grid96,1,1; block32,4,1; dynamic shared0. Ready/done adjacency512 bytes is preserved in replay.

Inputs,weights and outputs use distinct blocked layouts. The initial contiguous-layout implementation failed. Zero/impulse diagnostic probes and module address expressions identified the mappings; current handwritten mathematical implementation uses public PTX MMA fragment rules, FP16 accumulation, cubic SiLU and E4M3 output. Proprietary PTX/SASS was not embedded in the custom kernel.

## D. Capture/replay

Three fresh minimal captures at frames60,61,62, after lifecycle cleanup changes. Input/output/weight ranges, initial output, counters, expected output, parameters, module image and runtime identity are complete. Binary packets remain external diagnostic assets at `D:\Users\tcfialho\Documents\NRFusion\.temp\phase2a\ffn-validation-captures`; hashes/metadata are archived in `../phase2a/2026-09-30-replacement`.

| Frame | Expected, stock replay and custom replay SHA256 | Differences |
|---:|---|---:|
|60|`15caef635cd2f599c32c431ecc66fefc1ab53c76d11ac56a7b36b9e911b113e8`|0|
|61|`ec0a2dd0175b83d46dc76e4a34a5d9b62c5105f5c18e4d21522a2e5c9e2c64db`|0|
|62|`905a067fd1d49ed121a85386b958e83bc623e8f6d66a4582c2be96339b7417ca`|0|

All six comparisons passed byte equality, max/mean/RMS0, done-counter equality. Original three conservative captures also replayed exactly. The alleged coefficient prefix was disproved; weights capture now uses4MiB. Capture is diagnostic performance, never gameplay performance.

## E. Replacement and performance

Separate registry in `src/NrKernelReplacement.cpp`; dispatch stays in the existing native hook. Runtime hash, module hash, exact kernel name, actual GPU SM89, verified custom-asset hash,72-byte parameter configuration and exact launch shape gate selection. Unknown assets/configuration/init failure leave stock selected. Custom assets are retained until native function/module destruction so already-recorded command lists remain valid; eligibility is invalidated on feature release and revalidated with a fresh generation on creation. Capture staging references are released after fence retirement; observed resource catalogs are cleared on release.

| Variant | warmup | Samples | Median us | p95 us | p99 us |
|---|---:|---:|---:|---:|---:|
|Stock|50|300|56.320|58.368|265.216|
|Custom exact reference|50|300|801.792|976.896|1396.736|

Custom is approximately14.2x slower in this isolated run. This phase establishes deterministic substitution, not a performance gain. Driver compilation/cache state was not controlled; initialization CPU duration is reported separately by replay and is not claimed as a cold-JIT benchmark. Steady-state timing excludes warmup and ordering barriers. System-induced tails remain visible.

Live A/B:100 frames,40 measured,6240 records/mode;280 confirmed custom launches; final image SHA256 `e739c922f660bc366c6a5d9220594b4a97f4106389567d8dcdcd26054e34a83d` identical.

Lifecycle A/B:180 frames,140 measured,19656 kernel records/mode;749 confirmed custom launches. Feature release/recreate(frame60), quality change(frame80), input resolution1280x720→640x360(frame100)→1280x720(frame120), NR off(frame140)→on(frame150), and final release passed. Creation frames intentionally report PendingFeature/zero launches; every subsequent enabled frame requires successful NR. Unknown smaller shape used stock. Generations changed on recreation. Final image SHA256 `8fb4f4941a9c74196fd6dad3946ab86d513e5b71b47ac80788987a12c2cebbc9` identical. In-process D3D12 device recreation was not exercised: this testbed has no such operation; independent processes created/destroyed devices normally.

Asset fallback:one byte changed in an owned cubin; Custom mode produced zero replacements, stock output hash identical and exit0. NVIDIA installed binaries were not modified.

## F. Dominant-family classification and W4A8

All dominant families are inventoried; only the selected VIT FFN has a complete semantic ABI/replay/replacement contract. Swin FFWD structural aggregate56 bytes is known; other semantic ABIs/replays remain unresolved. No claim of replacing70% is made. The custom exact reference is a proven but slower candidate.

| Kernel/family | Share % | Semantic ABI | Stock replay | Replacement | Candidate |
|---|---:|---|---|---|---|
| cc_tinlayout_fused_swin_8h_256_8_chained_fp8 | 12.91 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_qkv_512_chained_fp8 | 7.73 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8 | 6.73 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_4h_128_4_chained_fp8 | 6.72 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_1h_32_1_chained_fp8 | 6.13 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_ffwd_512_chained_fp8 | 5.55 | unresolved (56-byte structural aggregate known) | not proven | not implemented | undetermined |
| cc_tinlayout_fused_post_block_swin_1h_32_fp8 | 5.42 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_ffn_contract_chained_fp8 | 4.91 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_ffn_expand_chained_fp8 | 4.47 | resolved | pass | exact; slower | yes, infrastructure reference |
| cc_tinlayout_fused_swin_2h_64_2_chained_fp8 | 4.06 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_qkv_chained_fp8 | 4.01 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_1h_32_1_ds_wait_fp8 | 3.18 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_ffwd_proj_512_chained_fp8 | 2.41 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_projection_chained_fp8 | 2.36 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_proj_512_chained_fp8 | 2.15 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_8h_256_8_inpview_tilesync_fp8 | 1.84 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_1h_32_1_upsample_tilesync_fp8 | 1.54 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_attention_chained_fp8 | 1.49 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_1h_32_1_inpview_tilesync_fp8 | 1.37 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_1h_32_1_outview_wait_fp8 | 1.24 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_2h_64_2_ds_wait_fp8 | 1.21 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_8h_256_8_outview_wait_fp8 | 1.20 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_8h_256_8_upsample_tilesync_fp8 | 1.17 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_8h_256_8_ds_wait_fp8 | 1.11 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_4h_128_4_upsample_tilesync_fp8 | 1.09 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_2h_64_2_inpview_tilesync_fp8 | 1.06 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_4h_128_4_ds_wait_fp8 | 1.02 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_2h_64_2_upsample_tilesync_fp8 | 1.02 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_2h_64_2_outview_wait_fp8 | 1.00 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_4h_128_4_outview_wait_fp8 | 0.86 | unresolved | not proven | not implemented | undetermined |
| cc_tinlayout_fused_swin_4h_128_4_inpview_tilesync_fp8 | 0.75 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_ffn_expand_publish_fp8 | 0.50 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_projection_wait_fp8 | 0.31 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_ffwd_inpview_512_tilesync_fp8 | 0.24 | unresolved | not proven | not implemented | undetermined |
| cg2r_copy_kernel | 0.24 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_proj_512_outview_wait_fp8 | 0.19 | unresolved | not proven | not implemented | undetermined |
| cc_dec_input_upsample_1024_512_tilesync_fp8 | 0.19 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_ffwd_proj_inpview_512_chained_fp8 | 0.18 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_proj_pool_512_chained_fp8 | 0.18 | unresolved | not proven | not implemented | undetermined |
| cc_split_swin_16h_final_head_512_wait_fp8 | 0.13 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_repack_2d_to_1d_fp8 | 0.06 | unresolved | not proven | not implemented | undetermined |
| cc_vit_1d_repack_1d_to_2d_fp8 | 0.05 | unresolved | not proven | not implemented | undetermined |
| cc_cb_clear | 0.02 | unresolved | not proven | not implemented | undetermined |

The optional W4A8 feasibility spike was executed only after stock replay and exact replacement gates passed. It used a local Marlin-inspired standalone prototype on the captured `M=288,K=1024,N=4096` shape; it is not the vLLM runtime or a production Marlin integration. Weight INT4 packing is one-time preparation outside frame timing; the INT8 path includes activation quantization in the timed path, while the FP8 path consumes the captured E4M3 activation directly.

| Shape | Stock FP8 reference | W4A16 | W4A8-I8 | W4A8-FP8 | Correctness |
|---|---:|---:|---:|---:|---|
|288x1024x4096|56.320 us|1752.064-1920.000 us|1400.832-1401.696 us|881.664-884.736 us|non-exact; 714034-720006 differing bytes; max abs 2.25-2.50|

Each value above uses50 warmups and300 timed samples per capture across three captures. All W4 variants were far slower than stock and numerically non-exact, so the Phase 2A gate rejects further W4A8 development for this kernel/shape. Broken existing W4A8 and FP16 fusion prototypes were not used or changed. No quantization,precision reduction,fusion,cadence,WorkingScale reduction,MGPU or W4A8 live integration was added. No whole-NR speedup is inferred. Raw final-validation evidence is in `../phase2a/2026-10-01-final-validation/summary.json`.

## Reproduction

All new diagnostics default OFF. Set discovery before feature creation:

```powershell
$env:NRFUSION_KERNEL_DISCOVERY='1'
$env:NRFUSION_KERNEL_REPLACEMENT_MODE='Custom' # or Stock
$env:NRFUSION_KERNEL_REPLACEMENT_CUBIN='D:\Users\tcfialho\Documents\NRFusion\build-windows-validation\generated\nrfusion_ffn_reference_sm89.cubin'
```

ABI and capture additionally use `NRFUSION_KERNEL_ABI`, `NRFUSION_KERNEL_ABI_DIR`, `NRFUSION_KERNEL_CAPTURE_DIR` and the proven `capture-contract.txt` archived with evidence. No argument arity guessing or allocation-wide readback is performed.

Build official targets `nrfusion_proxy`, `nrfusion_requiem_game`, `nrfusion_kernel_replay`, `nrfusion_ffn_reference` in Release. Run `RequiemGame.exe --require-nrfusion-proxy --require-nr --deterministic-motion --frames 180 --warmup 40 --nr-lifecycle --nr-kernels <trace.csv> --csv <frames.csv> --capture <output.ppm>` for the lifecycle contract.

`NRFusionKernelReplay <capture-directory> <output-directory> [custom-cubin]`; benchmark additionally sets `NRFUSION_REPLAY_BENCHMARK=1`. Diagnostic input probes deliberately return failure and never qualify correctness. Remove all diagnostic environment variables for normal execution.

## Validation / closure

Official Release targets `nrfusion_proxy`, `nrfusion_requiem_game`, `nrfusion_kernel_replay` and `nrfusion_ffn_reference` rebuilt successfully on2026-10-01. Focused identity,proxy-runtime and D3D12 diagnostic-resource CTests passed3/3. All six current stock/custom replays across captures0..2 are bit-identical with zero differing output bytes and zero error. Current lifecycle A/B produced19656 kernel records/mode,749 Custom replacements,zero drops and identical final SHA256 `8fb4f4941a9c74196fd6dad3946ab86d513e5b71b47ac80788987a12c2cebbc9`. The smaller lifecycle shape used133 stock launches and zero custom replacements. Runtime-hash and GPU-architecture negative gates rejected before producing output.

Installer/package regeneration and shipping-game installation were not performed for this diagnostic phase. No push was performed.

Technical debt:the exact reference is slow; arbitrary semantic ABIs remain unresolved; profiler event overhead10.036%; external Nsight per-kernel validation and device recreation qualification remain open. These were not turned into optimization work.

## References and handoff

Primary documentation:official NVIDIA CUDA13.4 driver/PTX documentation and NVAPI SDK70d337db9186e968eab622f7e786de7e437faf3d. Architecture references:[sdli capture/replay](https://github.com/sdli1995/dlssg_for_sm86) at9621db573e07ed54f50c15bbb585ed9a7bdfac28 (linked capture documentation unavailable at this snapshot; no copied capture source),[Tony Transfusion](https://github.com/TonyJoaca/DLSSG-Transfusion) at93ae5c1603c36935c865c79a7912be31f01ae7f1,and [neural-upstream](https://github.com/matiasLombo/neural-upstream) atc06c07b27c3c5af0d916c3d9545434735d624bdf. No runtime-specific optimization was imported.

Unknown capture runtime hash and SM120 metadata were rejected by the official replay executable before output/GPU execution. Fresh replay reports record module/function initialization CPU duration with explicitly unknown driver JIT cache state. Built/deployed proxy and testbed binary hashes match; manifest is archived with evidence. Final lifecycle A/B was rerun after deployment of both final binaries and passed again with the same image hash/counts.

Handoff: exact offline/live substitution passed; focused CTests3/3; six fresh stock/custom replays are exact; lifecycle A/B and fail-closed shape fallback passed; W4A8 feasibility was rejected by measured performance/correctness. Implementation HEAD before the final evidence commit is `33b6821a99ae0cbcbedf3c2b4381d4f55abe8e1f`. No push performed.

Completion-audit hardening:replacement identity is checked before interpreting the72-byte ABI; capture configuration accepts only the proven name,module hash,shape,field offsets and minimal ranges. Unqualified capture contracts leave capture disabled.

Final hardening validation:valid contract produced three complete captures and all six fresh stock/custom replays passed exactly. A contract changing the input field from offset0 to offset8 left capture disabled:official workload exit0,no capture packages. Rebuilt live A/B again passed with280 custom launches and identical image hash; focused CTests3/3 passed. Artifact hash manifest refreshed after deployment of the final official binaries.

Lifecycle A/B after final contract/identity hardening also passed:180 frames per process,749 Custom launches,identical final image. Full current revalidation is archived in `benchmarks/phase2a/2026-10-01-final-validation/summary.json`.
