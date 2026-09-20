# NRFusion Universal Standalone — Implementation Plan

## 1. Mission

Build NRFusion as a universal standalone DLSS 5 host instead of an OptiScaler extension.

The game is **not required to support DLSS**. Native DLSS/RR/Streamline integration is only one possible source of frame data.

Target:

- x64 and x86 games;
- D3D12;
- D3D11;
- D3D10;
- D3D9;
- Vulkan;
- OpenGL;
- native, bridge and fully synthetic frame providers;
- Neural Rendering as the primary universal DLSS 5 capability;
- Multi Frame Generation where a technically valid presentation/Streamline/DLSSG route can be built;
- a canonical 64-bit D3D12 DLSS 5 executor whenever possible;
- lower host CPU overhead than OptiScaler + NRFusion;
- equivalent or lower VRAM for an equivalent active feature set.

The project must never claim a backend is supported merely because it compiles. A backend becomes supported only after its acquisition, synchronization, execution and composition path has been reviewed and qualified.

---

## 2. Non-negotiable architecture

### 2.1 Universal frame contract

All graphics APIs must normalize into one API-independent contract.

Conceptually:

```cpp
struct FrameContract {
    FrameId frameId;

    ResourceRef color;
    ResourceRef depth;
    ResourceRef motion;
    ResourceRef exposure;

    Resolution renderResolution;
    Resolution outputResolution;

    Jitter jitter;
    MotionSource motionSource;

    bool depthReliable;
    bool motionReliable;
    bool hdr;
    bool cameraCut;
};
```

The core must not need to know whether a frame came from D3D12, D3D11, Vulkan, OpenGL, D3D10 or D3D9.

API-specific code belongs in carriers/providers.

### 2.2 Provider model

Preserve and formalize the existing three-route design:

- **Native**: use an existing usable DLSS/RR contract from the game.
- **Bridge**: the game exposes useful data but execution happens through another API/runtime path.
- **Synthetic**: the game has no usable DLSS contract; NRFusion builds the best valid contract from captured resources and generated guides.

Provider selection must never reduce to:

```text
No DLSS in game -> unsupported
```

It must be:

```text
Usable native contract?
  yes -> Native/Bridge
  no  -> Synthetic acquisition
```

### 2.3 Canonical executor

Prefer one canonical 64-bit D3D12 Neural Rendering executor.

```text
D3D12 -----------\
D3D11 ------------\
D3D10 -------------\
D3D9 --------------- > FrameContract -> NR Session -> D3D12 DLSS5 Executor
Vulkan -------------/
OpenGL ------------/
x86 carrier -------/
```

Do not create separate Neural Rendering implementations for every API unless a hard technical requirement proves it necessary.

### 2.4 x86 architecture

The DLSS NR model is treated as a 64-bit execution dependency.

x86 games use a thin in-process carrier:

```text
x86 game
  -> capture
  -> shared GPU resources / synchronization
  -> lightweight IPC control
  -> NRFusionHost64
  -> DLSS 5 executor
  -> shared result
  -> composition in game
```

The x86 carrier must not duplicate the policy engine, heavy diagnostics, model loader or large runtime state.

### 2.5 Fail closed

Unknown resource provenance, unknown feature identity, invalid synchronization, unsupported interop or ambiguous ownership must disable that route instead of guessing.

---

## 3. Performance contract

The standalone rewrite is accepted only if the architecture is lighter by design.

Steady-state normal operation targets:

```text
heap allocations/frame          = 0
D3D resource creation/frame     = 0
descriptor heap creation/frame  = 0
filesystem access/frame         = 0
string parsing/frame            = 0
config parsing/frame            = 0
shader compilation/frame        = 0
blocking GPU wait/frame         = 0
```

Additional requirements:

- prefer no mutex on the normal render path;
- no mutex may exist merely "for safety": actual concurrent writers/readers must be identified;
- configuration is parsed outside the frame path into compact runtime state;
- expensive diagnostics are opt-in;
- unused Advanced features do not allocate their GPU resources;
- equivalent feature configuration must use no more VRAM than the current implementation unless a measured, documented tradeoff explicitly justifies it.

The goal is primarily:

- lower host CPU cost;
- lower p95/p99 host latency;
- less synchronization noise;
- fewer auxiliary GPU commands;
- no extra VRAM.

No claim of improved real-game FPS or GPU frame time is valid until measured on real hardware.

---

## 4. Existing assets to preserve

The rewrite must reuse proven project work instead of recreating it blindly.

Existing architectural assets include:

- `FrameProvider::{Native, Bridge, Synthetic}`;
- `FrameContract`-like portable types already present in `Types.hpp`;
- `SyntheticDx11BridgeProvider`;
- `SyntheticVulkanProvider`;
- `SyntheticOpenGlProvider`;
- D3D11 capture;
- x86 capture/export layer;
- `HostServer64`;
- `HostDlssNr`;
- synthetic D3D12 execution;
- adaptive controller/runtime;
- telemetry/work identity infrastructure;
- existing MFG `DlssgTransfusion`;
- current D3D12 DLSS NR executor behavior and accumulated compatibility work.

Extraction comes before rewriting mature GPU code.

---

# PHASE 0 — Baseline and responsibility map

## Goal

Freeze the exact current behavior before changing ownership.

## Implementation checklist

- [ ] Record current master SHA and current shipped dependency revisions.
- [ ] List every NRFusion patch point inside OptiScaler.
- [ ] Classify each patch point as Host, Provider, Executor, MFG, Menu, Diagnostics or Compatibility.
- [ ] List current persistent D3D12 resources and their activation conditions.
- [ ] List readback resources and query heaps.
- [ ] List current hooks required for NR.
- [ ] List current hooks required for MFG.
- [ ] List OptiScaler hooks that NRFusion does not fundamentally need.
- [ ] Record current NR feature creation/rebuild conditions.
- [ ] Record current reset/history invalidation behavior.
- [ ] Record current failure/fallback behavior.
- [ ] Record current x86/Host64 transport contract.
- [ ] Record current Vulkan/OpenGL/D3D11 synthetic transport assumptions.

Each checkbox must be small enough to review independently.

## Mandatory review gate

Before Phase 1, the code review must answer:

1. What exact responsibility disappears if OptiScaler is removed?
2. Who owns that responsibility in the standalone design?
3. Which current behavior must be preserved exactly?
4. Which behavior exists only because OptiScaler is generic and may be removed?

No implementation starts until these answers are concrete.

---

# PHASE 1 — Formalize the universal FrameContract

## Goal

Make API-specific frame acquisition independent from NR policy/execution.

## Implementation checklist

- [ ] Audit current `FrameContext` fields against all existing providers.
- [ ] Define mandatory versus optional resources.
- [ ] Encode resource provenance explicitly.
- [ ] Encode depth reliability explicitly.
- [ ] Encode motion provenance explicitly.
- [ ] Encode motion reliability explicitly.
- [ ] Encode render/output dimensions explicitly.
- [ ] Encode reset/camera-cut semantics explicitly.
- [ ] Define HDR/exposure semantics.
- [ ] Define API-neutral resource identity/lifetime rules.
- [ ] Define frame/work identity across asynchronous providers.
- [ ] Ensure core policy can consume the contract without API casts.

## Review gate

Manually inspect every provider and answer:

- Can it populate each field honestly?
- Does any field imply reliability only because a pointer exists?
- Can stale resources survive a reset/reconfigure?
- Can frame N resources be confused with frame N+1?
- Is API-specific state leaking into policy code?

No synthetic provider may invent "native" provenance.

---

# PHASE 2 — Standalone runtime shell

## Goal

Create the host/runtime skeleton without executing Neural Rendering.

## Implementation checklist

- [ ] Standalone initialization lifecycle.
- [ ] Clean shutdown lifecycle.
- [ ] Compact runtime configuration snapshot.
- [ ] Minimal logging.
- [ ] Minimal status surface.
- [ ] Provider registration.
- [ ] Executor registration.
- [ ] No dependency on OptiScaler `Config`.
- [ ] No dependency on OptiScaler `State`.
- [ ] No Python patcher dependency at runtime.
- [ ] Disabled mode leaves game rendering untouched.

## Review gate

Inspect manually:

- DLL/process lifetime;
- COM ownership;
- module ownership;
- hook installation/removal;
- global mutable state;
- thread ownership;
- shutdown during partial initialization;
- repeated initialize/shutdown paths.

No GPU feature work is added in this phase.

---

# PHASE 3 — Feature identity registry

## Goal

Never confuse SR/RR/FG or unknown NGX features.

## Implementation checklist

- [ ] Intercept relevant feature creation.
- [ ] Store concrete feature type by handle.
- [ ] Record viewport/context identity where necessary.
- [ ] Handle creation failure without registry pollution.
- [ ] Remove entries on release.
- [ ] Handle recreation/resize.
- [ ] Handle handle reuse safely.
- [ ] Route SR correctly.
- [ ] Route RR correctly.
- [ ] Exclude FG from NR execution.
- [ ] Pass unknown features untouched.

## Hard rule

Never infer feature type only from "has depth + motion vectors".

## Review gate

Walk manually through:

- SR + FG in one game;
- RR + FG;
- multiple viewports;
- feature recreation;
- failed creation;
- late release;
- reused pointer/handle;
- unknown plugin/runtime versions.

It must be structurally impossible for FG evaluate to trigger NR accidentally.

---

# PHASE 4 — Canonical D3D12 DLSS 5 executor extraction

## Goal

Extract the mature D3D12 NR path before optimizing it.

## Preserve first

- feature creation;
- feature pending-submission rule;
- barriers;
- resource arrival/restoration states;
- padded/subrect handling;
- depth/motion compatibility handling;
- pre-SR;
- post-SR;
- RR;
- history;
- scaling below 100%;
- supersampling above 100%;
- multipass;
- HDR/exposure behavior;
- residual paths;
- failure latching/recovery;
- precision rebuilds.

## Implementation checklist

- [ ] Define explicit executor input.
- [ ] Define explicit executor result/status.
- [ ] Remove direct OptiScaler configuration reads.
- [ ] Remove direct OptiScaler state reads.
- [ ] Pass required values explicitly.
- [ ] Preserve resource allocation conditions.
- [ ] Preserve barrier ordering.
- [ ] Preserve early-return cleanup.
- [ ] Preserve feature rebuild semantics.
- [ ] Preserve reset/history semantics.
- [ ] Preserve fail-close behavior.

## Hard rule

Do not optimize barriers/resources while extracting.

First prove equivalent ownership and behavior. Optimization comes later.

## Review gate

For every moved resource:

```text
owner
creation condition
initial state
state during pass
final state
release condition
resize behavior
failure behavior
```

must be documented in the review.

---

# PHASE 5 — NrSession: one transaction per frame

## Goal

Remove adapter fragmentation and redundant synchronization.

## Target shape

```cpp
FrameResult NrSession::ProcessFrame(const FramePacket& frame);
```

## Implementation checklist

- [ ] Consolidate frame decision inputs.
- [ ] Consolidate work identity.
- [ ] Consolidate timing retirement.
- [ ] Consolidate performance policy.
- [ ] Consolidate precision policy.
- [ ] Consolidate provider status.
- [ ] Consolidate MFG-visible policy state.
- [ ] Remove redundant getters from the frame path.
- [ ] Remove repeated same-mutex entries.
- [ ] Preserve stale-timing rejection.
- [ ] Preserve scale-generation invalidation.
- [ ] Preserve reconfigure quarantine behavior.

## Review gate

Produce a before/after call-path review:

```text
old frame path:
  call A
  lock
  call B
  lock
  ...

new frame path:
  one coherent transaction
```

Every removed lock/call must have a concrete reason, not an aesthetic reason.

---

# PHASE 6 — D3D12 x64 carrier

## Goal

Complete the simplest native standalone route first without making it the architectural center.

## Implementation checklist

- [ ] Acquire device.
- [ ] Acquire relevant queue.
- [ ] Acquire frame resources.
- [ ] Build valid FrameContract.
- [ ] Integrate feature registry.
- [ ] Execute canonical NR executor.
- [ ] Compose result.
- [ ] Handle resize.
- [ ] Handle device loss/removal.
- [ ] Handle frame without usable depth/motion.
- [ ] Handle NR disabled with near-zero work.

## Review gate

Trace one complete frame from intercepted call to return.

Check that:

- game resource ownership is not stolen;
- no CPU copy is introduced;
- no per-frame resource creation exists;
- synchronization does not stall the game thread unnecessarily.

---

# PHASE 7 — Timing and diagnostics split

## Goal

Keep controller timing while removing always-on detailed instrumentation.

## Normal path target

```text
2 timestamps
1 ResolveQueryData
1 fixed timing ring
persistent/cached read path
```

## Implementation checklist

- [ ] One shared query heap/ring.
- [ ] Queue frequency cached until queue changes.
- [ ] No per-frame timer object creation.
- [ ] No synchronous query wait.
- [ ] Timing result associated with exact work identity.
- [ ] Detailed model timing disabled by default.
- [ ] Resolve-stage timing disabled by default.
- [ ] Detailed timing enabled only through Diagnostics.

## Review gate

Compare current and new command emission manually.

Count:

- timestamp commands;
- resolves;
- readback operations;
- CPU calls to queue frequency;
- allocations.

Diagnostics-off must not pay Diagnostics-on cost.

---

# PHASE 8 — D3D11 x64 bridge

## Goal

Promote the existing D3D11 bridge into a qualified carrier.

## Implementation checklist

- [ ] Review current D3D11 -> shared D3D12 resource path.
- [ ] Remove unnecessary CPU waits.
- [ ] Validate slot/fence ownership.
- [ ] Validate resource formats.
- [ ] Capture color.
- [ ] Capture usable depth where possible.
- [ ] Capture native motion where possible.
- [ ] Fall back to DLSS-contract/NVOF/shader/zero motion explicitly.
- [ ] Execute canonical D3D12 NR.
- [ ] Compose back to D3D11.
- [ ] Handle resize/recreate.
- [ ] Handle device/context destruction.

## Review gate

Special attention:

- keyed mutex/fence semantics;
- allocator reuse only after GPU retirement;
- no hidden CPU readback;
- no frame N/N+1 slot aliasing;
- no unconditional extra full-frame copy when an interop path can avoid it.

---

# PHASE 9 — x86 Carrier + Host64

## Goal

Make bitness independent from DLSS 5 execution.

## Implementation checklist

- [ ] Keep x86 in-process code minimal.
- [ ] Reuse existing CaptureProvider32 transport.
- [ ] Reuse HostServer64 architecture.
- [ ] Version the IPC contract explicitly.
- [ ] Transport frame/work identity.
- [ ] Transport configuration generation.
- [ ] Transport reset state.
- [ ] Transport shared resource handles.
- [ ] Transport producer/consumer synchronization.
- [ ] Handle host crash/disconnect.
- [ ] Handle game exit during pending work.
- [ ] Handle host restart only at a safe session boundary.
- [ ] Keep policy/controller on the 64-bit side.
- [ ] Keep DLSS model loading on the 64-bit side.

## Hard rule

No full-frame CPU IPC transport.

IPC is for control; pixels remain GPU-resident through shared resources/interoperability.

## Review gate

Manually review all ownership boundaries:

- which process owns each HANDLE;
- who duplicates it;
- who closes it;
- fence direction;
- session generation;
- stale handle rejection.

---

# PHASE 10 — Vulkan carrier

## Goal

Promote the existing Vulkan synthetic route into a qualified provider.

## Implementation checklist

- [ ] Audit external-memory requirements.
- [ ] Audit Win32 handle ownership.
- [ ] Audit timeline semaphore import.
- [ ] Correct Vulkan memory type selection.
- [ ] Correct image usage flags.
- [ ] Correct queue-family ownership.
- [ ] Correct layout transitions.
- [ ] Capture color.
- [ ] Acquire depth/motion where possible.
- [ ] Build explicit provenance/reliability.
- [ ] Execute through canonical D3D12 host.
- [ ] Compose result back.
- [ ] Handle swapchain recreation.
- [ ] Handle device recreation.

## Review gate

No hard-coded Vulkan structure constants or memory assumptions may remain without proof against the runtime headers/spec contract used by the project.

A compile-only Vulkan path is not considered supported.

---

# PHASE 11 — OpenGL carrier

## Goal

Finish and qualify the existing OpenGL -> D3D12 interop route.

## Existing basis

The project already has an OpenGL provider using Win32 external memory/semaphore concepts.

## Implementation checklist

- [ ] Integrate OpenGL into ProviderPolicy.
- [ ] Separate "carrier exists" from "interop qualified".
- [ ] Validate required extensions at runtime.
- [ ] Validate active context.
- [ ] Validate memory-object import.
- [ ] Validate semaphore/fence import.
- [ ] Capture source texture GPU-side.
- [ ] Build valid FrameContract.
- [ ] Execute canonical D3D12 NR.
- [ ] Compose/copy result back GPU-side.
- [ ] Handle context recreation.
- [ ] Handle resolution changes.
- [ ] Handle unsupported extensions explicitly.

## Review gate

No OpenGL support claim unless:

- no CPU pixel readback;
- synchronization direction is correct;
- imported object lifetime is correct;
- failure disables the route cleanly.

---

# PHASE 12 — D3D10 carrier

## Goal

Add a D3D10 bridge instead of a separate NR implementation.

## Implementation checklist

- [ ] Identify practical DXGI shared-resource route.
- [ ] Acquire device/adapter identity.
- [ ] Capture color GPU-side.
- [ ] Acquire depth if technically available.
- [ ] Determine motion source strategy.
- [ ] Share/bridge into canonical execution path.
- [ ] Synchronize without per-frame blocking where possible.
- [ ] Compose result back.
- [ ] Handle resize/device recreation.
- [ ] Fail closed on unsupported resource sharing.

## Review gate

Do not add a permanent extra copy merely to make the code simpler if resource sharing can remove it.

Where copying is unavoidable, document exactly why.

---

# PHASE 13 — D3D9 carrier

## Goal

Support legacy D3D9 without implementing a second NR runtime.

## Implementation checklist

- [ ] Identify D3D9/D3D9Ex interception route.
- [ ] Determine efficient GPU transfer/bridge options.
- [ ] Separate D3D9Ex capability from classic D3D9 limitations.
- [ ] Capture color.
- [ ] Determine depth acquisition strategy.
- [ ] Determine motion strategy.
- [ ] Bridge to canonical 64-bit D3D12 execution.
- [ ] Compose result back.
- [ ] Handle reset/lost-device semantics.
- [ ] Ensure unsupported cases report a clear route failure.

## Review gate

D3D9 support must not silently degrade into CPU screenshot processing.

If GPU-resident transport cannot be built for a route, that route is not yet qualified.

---

# PHASE 14 — Universal guide acquisition

## Goal

Make "game has no DLSS" a normal path.

## Motion priority

Use the best proven source available:

```text
Native game motion
  -> DLSS/RR contract motion
  -> NVOF
  -> shader-estimated motion
  -> explicit zero fallback
```

## Depth priority

- native reliable depth;
- bridge/captured depth with proven semantics;
- route-specific fallback if valid;
- explicit absence.

## Implementation checklist

- [ ] Centralize motion-source selection.
- [ ] Centralize depth reliability decisions.
- [ ] Never infer reliability from non-null resource alone.
- [ ] Record provenance in FrameContract.
- [ ] Ensure controller/placement only uses capabilities actually present.
- [ ] Reset temporal history when guide provenance changes materially.

## Review gate

For each API/provider combination, produce a small table:

```text
color source
depth source
motion source
exposure source
reliability
fallback
```

---

# PHASE 15 — MFG standalone

## Goal

Preserve the useful MFG functionality without carrying generic OptiScaler machinery.

## User-visible behavior to preserve

- Game Controlled;
- 2X;
- 3X;
- 4X;
- Dynamic;
- Performance;
- Enhanced;
- UI Recomposition;
- 5X/6X experimental where the existing implementation supports them.

## Implementation checklist

- [ ] Extract `DlssgTransfusion` host dependencies.
- [ ] Convert config strings only at load/menu-change time.
- [ ] Use compact enum/atomic state in hot hooks.
- [ ] Remove hot-path mutex where concurrency analysis proves it unnecessary.
- [ ] Keep patch scanning load-time only.
- [ ] Preserve safe multiplier transition.
- [ ] Preserve late module load handling.
- [ ] Preserve state publication to the game.
- [ ] Separate MFG availability from NR availability.
- [ ] Report unsupported MFG routes clearly instead of affecting NR.

## Review gate

Review:

- Streamline versions;
- SetOptions frequency;
- GetState behavior;
- late DLSSG load;
- Dynamic mode;
- rapid multiplier changes;
- game-native FG state;
- HUDless/UI recomposition;
- absence of DLSSG.

Universal NR support must not be blocked because a game cannot support the same MFG route.

---

# PHASE 16 — Menu and configuration

## Main menu target

```text
NRFusion
Enabled                     [ On ]

Neural Rendering
Mode                        [ Auto ]
Target FPS                  [ 120 ] [ Display Hz ]

Multi Frame Generation
Mode                        [ Game Controlled ]
Quality                     [ Performance ]

Status
NR Scale                    0.78x
NR Cost                     2.31 ms
MFG                         4X Active
Execution                   Async
State                       Stable

Advanced                    [ > ]
```

## Rules

- Main menu remains intentionally small.
- Advanced is collapsed by default.
- Opening/closing menu does not alter runtime policy.
- Closed menu does not build hundreds of invisible widgets.
- Config is parsed outside the frame path.
- Render thread consumes compact runtime state.

## Advanced may expose

- precision override;
- DLSS 5 visual tuning;
- exposure/HDR overrides;
- residual/placement;
- multipass;
- MFG experimental controls;
- comparison/debug tools;
- detailed timing;
- recalibration;
- copy diagnostics.

## Review gate

Every Advanced option must answer:

1. Is this still useful?
2. Does Auto already handle it?
3. Does it allocate resources while inactive?
4. Does it add steady-state work while hidden?

If the answer to 3 or 4 is yes, redesign it.

---

# PHASE 17 — State restoration and compatibility

## Goal

Preserve difficult-engine compatibility without imposing generic compatibility overhead on every game.

## Implementation checklist

- [ ] Identify cases that truly require root signature restoration.
- [ ] Identify descriptor-heap restoration cases.
- [ ] Identify PSO restoration cases.
- [ ] Capture the minimum state necessary.
- [ ] Keep compatibility behavior profile-driven/capability-driven.
- [ ] Avoid global generic maps when the current game does not require them.
- [ ] Preserve bindless-engine compatibility.

## Review gate

Every state item captured must have a concrete game/API reason.

Do not keep generic state tracking because OptiScaler happened to need it for unrelated features.

---

# PHASE 18 — Resource/VRAM discipline

## Goal

Make inactive features consume no unnecessary VRAM.

## Required lazy allocation behavior

```text
multipass == 1
  -> no extra pass scratch
  -> no extra pass features

residual disabled
  -> no residual history resources

hold frame disabled
  -> no held frame resource

diagnostics disabled
  -> no diagnostic-only GPU resources
```

## Implementation checklist

- [ ] Inventory every GPU resource.
- [ ] Record size formula.
- [ ] Record activation condition.
- [ ] Record reuse policy.
- [ ] Record release condition.
- [ ] Record resize behavior.
- [ ] Ensure feature toggles do not leak old allocations.
- [ ] Avoid duplicate resources across carrier/executor where interop allows reuse.

## Review gate

For equivalent active features:

```text
standalone VRAM <= current OptiScaler+NRFusion VRAM
```

Any exception requires measured benefit and explicit approval.

---

# PHASE 19 — Hot-path audit

## Goal

Enforce "lighter by design" after all routes exist.

## Manual code-review search list

Review all normal frame paths for:

- [ ] `new`;
- [ ] `delete`;
- [ ] `make_unique` / `make_shared`;
- [ ] `std::vector` growth;
- [ ] transient `std::string`;
- [ ] `std::map` / `unordered_map` insertion;
- [ ] `std::function`;
- [ ] stream formatting;
- [ ] filesystem calls;
- [ ] module scans;
- [ ] PE/fatbin scans;
- [ ] mutex/shared_mutex;
- [ ] `CreateCommittedResource`;
- [ ] `CreatePlacedResource`;
- [ ] `CreateDescriptorHeap`;
- [ ] `CreateQueryHeap`;
- [ ] PSO creation;
- [ ] synchronous fence waits;
- [ ] repeated capability queries;
- [ ] repeated timestamp-frequency queries;
- [ ] string-to-enum conversion.

Every remaining occurrence needs a concrete justification.

---

# PHASE 20 — Measured executor optimization

## Goal

Only now optimize the mature GPU path.

Potential candidates:

- unnecessary copies;
- redundant barriers;
- descriptor rewrites;
- exposure courier/readback;
- timing commands;
- resolve work;
- resource transitions.

## Required sequence

```text
observe
-> identify exact cost
-> prove operation is redundant or replaceable
-> preserve state/lifetime semantics
-> change
-> review
-> measure again
```

No barrier is removed because it "looks redundant".

---

# PHASE 21 — Qualification matrix

A backend becomes supported only after its gate passes.

## Matrix

| API | x64 | x86 | Native | Bridge | Synthetic | Qualification |
|---|---:|---:|---:|---:|---:|---|
| D3D12 | target | carrier/host if needed | yes | where useful | yes | required |
| D3D11 | target | target | where contract exists | yes | yes | required |
| D3D10 | target | target | n/a | target | target | required |
| D3D9 | target | target | n/a | target | target | required |
| Vulkan | target | target where transport permits | where contract exists | yes | yes | required |
| OpenGL | target | target where transport permits | n/a | yes | yes | required |

"Target" is not "supported". Only qualification changes status to supported.

## Per-route qualification gate

- [ ] real resource acquisition path;
- [ ] GPU-resident transport;
- [ ] correct synchronization;
- [ ] correct ownership/lifetime;
- [ ] NR actually executes;
- [ ] result composes correctly;
- [ ] resize/recreation handled;
- [ ] failure path clean;
- [ ] no hidden CPU frame transport;
- [ ] no steady-state resource creation;
- [ ] no known VRAM regression;
- [ ] no known frame identity race.

---

# PHASE 22 — A/B and OptiScaler cutover

## CPU comparison

Compare current OptiScaler-hosted NRFusion against standalone:

- CPU ns/frame;
- p50;
- p95;
- p99;
- allocations/frame;
- allocated bytes/frame;
- lock acquisitions/frame;
- D3D resource creations during steady state;
- number of timing/query commands.

## Real GPU comparison

Must be run on real compatible hardware:

- GPU frame time;
- NR GPU time;
- average FPS;
- 1% low;
- p95/p99 frame time;
- VRAM;
- MFG pacing;
- visual equivalence/stability.

## Cutover gate

OptiScaler is removed from the primary distribution only when:

- [ ] D3D12 x64 standalone route qualified;
- [ ] universal FrameContract stable;
- [ ] feature registry proven;
- [ ] x86 Host64 route qualified;
- [ ] D3D11 route qualified;
- [ ] Vulkan route qualified;
- [ ] OpenGL route qualified;
- [ ] D3D10/D3D9 status explicitly known;
- [ ] MFG behavior preserved on supported routes;
- [ ] menu contract preserved;
- [ ] no steady-state heap allocation in normal path;
- [ ] no steady-state GPU-resource creation;
- [ ] normal timing path minimized;
- [ ] no known VRAM regression;
- [ ] host CPU overhead demonstrably below current host for equivalent work;
- [ ] real hardware A/B shows no material regression;
- [ ] fallback/recovery behavior is clear.

During transition OptiScaler remains available as a fallback/reference path.

---

# 5. Rigid code-review rules

These rules are more important than writing large automatic test suites.

## Rule 1 — Every hot-path change answers four questions

Before accepting it:

1. What executed before?
2. What executes after?
3. What concrete work was removed/added?
4. What externally observable behavior was preserved?

If these cannot be answered, the change is not reviewable.

## Rule 2 — No abstraction without cost accounting

Any new layer on the frame path must identify:

- dispatch cost;
- ownership;
- synchronization;
- memory;
- lifetime.

## Rule 3 — No mutex without demonstrated concurrency

"Safer" is not enough.

Review must identify:

- writer thread(s);
- reader thread(s);
- overlapping lifetime;
- consequence of lock removal.

## Rule 4 — Dynamic allocation review is broader than searching for new

Inspect:

- vectors;
- strings;
- maps;
- hash maps;
- formatting;
- function wrappers;
- filesystem;
- hidden library allocations.

## Rule 5 — GPU resource creation is event-driven only

Resource/heap/PSO creation is allowed only for:

- initialization;
- resolution/device reconfiguration;
- first activation of a feature;
- explicit capability transition.

Not normal frame execution.

## Rule 6 — Barrier changes require state proof

For a barrier to be removed/reordered, review must state:

- known previous state;
- required next state;
- caller guarantee;
- what happens on alternate path/early return.

## Rule 7 — COM/native ownership is explicit

For every native object:

- creator;
- owner;
- AddRef source;
- Release location;
- replacement behavior;
- device-loss behavior.

## Rule 8 — Error paths are first-class

Review at least:

- feature creation failure;
- unsupported format;
- missing depth;
- missing motion;
- interop failure;
- resize;
- device recreation/removal;
- model load failure;
- precision rebuild failure;
- late DLSSG load;
- x86 host disconnect.

## Rule 9 — No silent fallback

Fallback must update status/logs and preserve clear provenance.

Never make a synthetic/zero path look native.

## Rule 10 — Do not rewrite mature GPU behavior during extraction

Move first. Optimize after a stable ownership boundary exists.

## Rule 11 — Compatibility exceptions do not tax every game

Game/API-specific work must be gated.

## Rule 12 — Advanced/Diagnostics inactive means inactive

Hidden developer features must not allocate VRAM or add GPU work merely because code exists.

---

# 6. Review-first validation policy

Automatic tests are used selectively for contracts where they buy real confidence.

Do not create tests merely to inflate coverage.

Prefer code review for:

- ownership;
- lifetime;
- hot-path allocation;
- synchronization reasoning;
- barrier/state reasoning;
- fallback semantics;
- configuration flow.

Prefer focused automated tests for:

- deterministic controller/policy logic;
- serialization/IPC contracts;
- feature-handle registry;
- bounded data structures;
- regressions that previously caused a concrete failure.

Prefer real runtime validation for:

- graphics interop;
- synchronization;
- GPU state;
- resource barriers;
- VRAM;
- performance;
- image quality;
- frame pacing.

---

# 7. Git and CI discipline for this project

Each implementation item must be independently reviewable and small enough for one focused iteration.

Workflow:

```text
small local/detached commits
-> inspect diff
-> review ownership/performance implications
-> fix all known issues
-> stabilize batch
-> update branch once
-> one relevant CI pass
```

Do not:

```text
push -> build -> tiny fix -> push -> build -> tiny fix -> push -> build
```

Before any new full build:

- inspect existing build status;
- inspect existing failure logs;
- batch all known corrections.

CI confirms integration. It does not replace code review.

---

# 8. Architectural acceptance statement

The standalone effort is successful only if this statement is true:

> NRFusion can provide DLSS 5 Neural Rendering to a game regardless of whether the game originally integrated DLSS, using the best qualified Native, Bridge or Synthetic route for its graphics API and bitness; all routes normalize into one explicit frame contract and, whenever technically possible, execute through one canonical 64-bit D3D12 DLSS 5 backend. The rewrite must be lighter than the OptiScaler-hosted architecture in steady-state host overhead without increasing equivalent-feature VRAM, while preserving difficult compatibility behavior through isolated, justified compatibility paths.
