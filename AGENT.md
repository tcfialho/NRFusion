# Agent workflow

- Protect work continuously with Git and small commits.
- At session start, identify current state, last relevant commit, what works, and the exact next action.
- Before implementation, keep a checklist of independent, verifiable items small enough for one <=25 minute interaction.
- Measure real wall-clock session time. At 20 minutes stabilize; at 23 minutes freeze, checkpoint, and hand off.
- Prioritize delivery/integration blockers over refinements.
- For bugs: reproduce -> cause -> fix -> regression test -> full-flow validation.
- Preserve state before destructive or risky changes.
- Use nohup + PID + persistent log for medium/long jobs when shell execution is available.
- Before restarting interrupted work, inspect existing process/log state and do not duplicate heavy jobs.
- Keep useful logs/checkpoints progressively. Do not depend on a final save only.
- Generate one source checkpoint ZIP per session outside the worktree when source code changed materially.
- End with: result | tests | commit | ZIP | blocker | exact next action.

## Build discipline

- Do not update a remote feature branch after every commit when PR synchronization triggers expensive CI.
- Accumulate small detached/local commits while code is changing.
- Do not repeatedly run full builds while code is changing.
- Inspect existing CI state/logs before deciding whether another build is necessary.
- Move the branch ref only when the current change set is stable enough for validation.
- Prefer one final full build for the completed change set.
- If another fix is required after that build, batch all known fixes before the next remote update.
- Keep CI concurrency/cancellation enabled so obsolete runs do not consume runners.

## Source size

- Immediate rule: every new or substantively modified first-party handwritten code file for standalone work must be <=300 physical lines after formatting.
- Blank lines and comments count. The rule is intentionally mechanical.
- Soft limit: at ~250 lines, stop adding responsibility and split before reaching 300.
- Applies to production code, headers, CUDA, shaders, tests, harnesses, tools, scripts, CMake/build logic and installer code.
- Transition-only grandfathering: existing >300-line first-party files may remain read-only/no-growth until their owning phase splits or retires them.
- Final cutover requires zero first-party handwritten code files above 300 lines.
- Generated-file exemption is valid only when the file is reproducibly produced from tracked inputs, lives under a generated directory, is marked generated, and is not manually edited. The checker verifies location/marker; review verifies reproducibility.
- Vendored/third-party code and upstream fixtures are exempt only while unmodified. Local handwritten modifications count as first-party code.
- Do not evade the cap with minification, multiple statements per line, giant embedded code strings, generated-style .inc dumps, or by moving implementation into headers.
- Split by responsibility/lifetime/ownership, never arbitrary Part1/Part2 chunks or one God class spread across partial files.
- The 300-line rule must not create runtime cost: do not add virtual dispatch, heap/PImpl, shared ownership, locks, atomics, or indirect calls solely to split files.
- Splitting hot code across translation units can change inlining/optimization. Measure before/after when the split touches a measured hot path; preserve small inline helpers only when justified.

## Comments

- Prefer clearer names or extracted functions over comments.
- Comment only why something non-obvious is necessary, never restate what the code does.
- No narrative architecture headers, decorative arrows, or comments longer than 120 characters.

## Current session

Start: 2026-09-21 09:09 BRT
Branch: standalone/phase-03-development-harness-20260921
Base phase-02: 7fa6f9617d24675bef2bb2b0c571fe58763a345d
Phase 03: implementation structurally complete; Windows execution pending.

Completed:
- audited sim/harness/synthetic/IPC surfaces before creating code
- split the 1084-line D3D12 harness mechanically by responsibility
- proved original function bodies and shader were preserved before feature changes
- kept the existing executable/CTest; no parallel mini-game
- added correctness/benchmark dispatch through the same CLI
- added focused steady/resize/reset/missing-guides/provenance/on-off/failure scenarios
- resize scenario recreates real harness resources and restores the original dimensions
- benchmark measures AcquireFrame + ReadyForCore + ResolveAuto only
- benchmark uses preallocated samples and reports p50/p95/p99 plus failure counters
- benchmark baseline produces automatic before/after output outside the measured region
- audited changed source size and comments; all <=300, no long comment blocks
- preserved >300 legacy tests read-only
- no PR or CI run created

Blocker:
- this environment has no Windows SDK/Mingw
- available GitHub connector can read/rerun workflows but cannot dispatch workflow_dispatch

Exact next action:
- run one stabilized Windows validation for this Phase 03 head
- execute nrfusion_harness_3d correctness, --scenario all and --benchmark
- fix only observed regressions, then close Phase 03; do not start Phase 04 yet
